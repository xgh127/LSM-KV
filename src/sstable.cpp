#include "sstable.h"

#include <algorithm>
#include <cstring>
#include <fstream>

namespace mini_lsm {

namespace {

std::uint16_t read_u16le(const char* p) {
    return static_cast<std::uint16_t>(
        static_cast<unsigned char>(p[0]) |
        (static_cast<unsigned char>(p[1]) << 8));
}

std::uint32_t read_u32le(const char* p) {
    return static_cast<std::uint32_t>(
        static_cast<unsigned char>(p[0]) |
        (static_cast<unsigned char>(p[1]) << 8) |
        (static_cast<unsigned char>(p[2]) << 16) |
        (static_cast<unsigned char>(p[3]) << 24));
}

std::uint64_t read_u64le(const char* p) {
    std::uint64_t v = 0;
    for (int i = 0; i < 8; ++i) {
        v |= static_cast<std::uint64_t>(static_cast<unsigned char>(p[i])) << (i * 8);
    }
    return v;
}

} // anonymous namespace

SSTable::SSTable(std::uint64_t id, std::filesystem::path path)
    : id_(id), path_(std::move(path)) {}

bool SSTable::may_contain(KeyView key) const {
    if (block_metas_.empty()) return false;
    if (key < block_metas_.front().first_key) return false;
    if (key > block_metas_.back().last_key) return false;
    return true;
}

std::size_t SSTable::find_block_idx(KeyView key) const {
    // binary search on last_key: find first block where last_key >= key
    auto it = std::lower_bound(block_metas_.begin(), block_metas_.end(), key,
                               [](BlockMeta const& m, KeyView k) {
                                   return m.last_key < k;
                               });
    return static_cast<std::size_t>(it - block_metas_.begin());
}

Status SSTable::read_block(std::size_t idx, std::string& out) const {
    if (idx >= block_metas_.size()) {
        return Status::IOError("block index out of range");
    }
    auto const& meta = block_metas_[idx];
    std::uint32_t block_size;
    if (idx + 1 < block_metas_.size()) {
        block_size = block_metas_[idx + 1].offset - meta.offset;
    } else {
        block_size = static_cast<std::uint32_t>(block_data_end_ - meta.offset);
    }

    std::ifstream f(path_, std::ios::in | std::ios::binary);
    if (!f.is_open()) {
        return Status::IOError("cannot open " + path_.string() + " for reading");
    }
    f.seekg(meta.offset, std::ios::beg);
    out.resize(block_size);
    f.read(out.data(), static_cast<std::streamsize>(block_size));
    if (!f) {
        return Status::IOError("read_block: short read");
    }
    return Status::OK();
}

Status SSTable::open(std::uint64_t id, std::filesystem::path path,
                     std::unique_ptr<SSTable>& out) {
    if (!std::filesystem::exists(path)) {
        return Status::IOError("file not found: " + path.string());
    }

    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f.is_open()) {
        return Status::IOError("cannot open " + path.string() + " for reading");
    }

    // Read footer (last 24 bytes)
    f.seekg(-24, std::ios::end);
    if (!f) {
        return Status::IOError("file too small for footer");
    }
    char footer[24];
    f.read(footer, 24);
    if (!f) {
        return Status::IOError("cannot read footer");
    }
    auto meta_offset = read_u64le(footer);
    auto bloom_offset = read_u64le(footer + 8);
    auto magic = read_u64le(footer + 16);

    if (magic != SSTable::kMagic) {
        return Status::IOError("bad magic number");
    }

    // Read meta
    f.seekg(static_cast<std::streamoff>(meta_offset), std::ios::beg);
    char meta_count_buf[4];
    f.read(meta_count_buf, 4);
    auto num_blocks = read_u32le(meta_count_buf);

    std::vector<BlockMeta> metas;
    metas.reserve(num_blocks);
    for (std::uint32_t i = 0; i < num_blocks; ++i) {
        char offset_buf[4];
        f.read(offset_buf, 4);
        auto blk_off = read_u32le(offset_buf);

        char num_ent_buf[4];
        f.read(num_ent_buf, 4);
        auto num_ent = read_u32le(num_ent_buf);

        char fklen_buf[2];
        f.read(fklen_buf, 2);
        auto fk_len = read_u16le(fklen_buf);
        std::string first_key(fk_len, '\0');
        f.read(first_key.data(), fk_len);

        char lklen_buf[2];
        f.read(lklen_buf, 2);
        auto lk_len = read_u16le(lklen_buf);
        std::string last_key(lk_len, '\0');
        f.read(last_key.data(), lk_len);

        BlockMeta m;
        m.offset = blk_off;
        m.num_entries = num_ent;
        m.first_key = std::move(first_key);
        m.last_key = std::move(last_key);
        metas.push_back(std::move(m));
    }

    // Read bloom (optional)
    std::vector<unsigned char> bloom;
    if (bloom_offset != 0) {
        f.seekg(static_cast<std::streamoff>(bloom_offset), std::ios::beg);
        char bloom_len_buf[4];
        f.read(bloom_len_buf, 4);
        auto bloom_bytes = read_u32le(bloom_len_buf);
        if (bloom_bytes > 0) {
            bloom.resize(bloom_bytes);
            f.read(reinterpret_cast<char*>(bloom.data()), bloom_bytes);
        }
    }

    f.close();

    auto table = std::make_unique<SSTable>(id, path);
    table->block_metas_ = std::move(metas);
    table->bloom_ = std::move(bloom);
    table->block_data_end_ = meta_offset;
    out = std::move(table);
    return Status::OK();
}

// SSTableIterator ------------------------------------------------------------
SSTableIterator::SSTableIterator(std::shared_ptr<const SSTable> table)
    : table_(std::move(table)) {}

bool SSTableIterator::is_valid() const { return valid_; }

KeyView SSTableIterator::key() const {
    if (!valid_ || current_block_entries_.empty()) return {};
    return current_block_entries_[entry_cursor_].first;
}

ValueView SSTableIterator::value() const {
    if (!valid_ || current_block_entries_.empty()) return {};
    return current_block_entries_[entry_cursor_].second;
}

Status SSTableIterator::next() {
    if (!valid_) return Status::OK();
    ++entry_cursor_;
    if (entry_cursor_ >= current_block_entries_.size()) {
        // move to next block
        ++current_block_idx_;
        if (current_block_idx_ >= table_->num_blocks()) {
            valid_ = false;
            return Status::OK();
        }
        return load_current_block();
    }
    return Status::OK();
}

Status SSTableIterator::load_current_block() {
    current_block_entries_.clear();
    entry_cursor_ = 0;
    std::string raw;
    auto s = table_->read_block(current_block_idx_, raw);
    if (!s.ok()) {
        valid_ = false;
        return s;
    }
    auto count = read_u32le(raw.data());
    std::size_t parse_pos = 4;
    current_block_entries_.reserve(count);
    for (std::uint32_t j = 0; j < count; ++j) {
        if (parse_pos + 2 > raw.size()) break;
        auto klen = read_u16le(raw.data() + parse_pos);
        parse_pos += 2;
        if (parse_pos + klen > raw.size()) break;
        Key k{raw.substr(parse_pos, klen)};
        parse_pos += klen;
        if (parse_pos + 4 > raw.size()) break;
        auto vlen = read_u32le(raw.data() + parse_pos);
        parse_pos += 4;
        if (parse_pos + vlen > raw.size()) break;
        Value v{raw.substr(parse_pos, vlen)};
        parse_pos += vlen;
        current_block_entries_.emplace_back(std::move(k), std::move(v));
    }
    if (current_block_entries_.empty()) {
        valid_ = false;
        return Status::OK();
    }
    valid_ = true;
    return Status::OK();
}

void SSTableIterator::seek_to_first() {
    if (table_->num_blocks() == 0) {
        valid_ = false;
        return;
    }
    current_block_idx_ = 0;
    entry_cursor_ = 0;
    load_current_block();
}

void SSTableIterator::seek_to_key(KeyView target) {
    auto idx = table_->find_block_idx(target);
    if (idx >= table_->num_blocks()) {
        valid_ = false;
        return;
    }
    current_block_idx_ = static_cast<std::uint32_t>(idx);
    current_block_entries_.clear();
    entry_cursor_ = 0;
    std::string raw;
    auto s = table_->read_block(static_cast<std::size_t>(idx), raw);
    if (!s.ok()) {
        valid_ = false;
        return;
    }
    auto count = read_u32le(raw.data());
    std::size_t parse_pos = 4;
    current_block_entries_.reserve(count);
    for (std::uint32_t j = 0; j < count; ++j) {
        if (parse_pos + 2 > raw.size()) break;
        auto klen = read_u16le(raw.data() + parse_pos);
        parse_pos += 2;
        if (parse_pos + klen > raw.size()) break;
        Key k{raw.substr(parse_pos, klen)};
        parse_pos += klen;
        if (parse_pos + 4 > raw.size()) break;
        auto vlen = read_u32le(raw.data() + parse_pos);
        parse_pos += 4;
        if (parse_pos + vlen > raw.size()) break;
        Value v{raw.substr(parse_pos, vlen)};
        parse_pos += vlen;
        current_block_entries_.emplace_back(std::move(k), std::move(v));
    }
    // linear scan within block
    for (std::size_t i = 0; i < current_block_entries_.size(); ++i) {
        if (current_block_entries_[i].first >= target) {
            entry_cursor_ = static_cast<std::uint32_t>(i);
            valid_ = true;
            return;
        }
    }
    valid_ = false;
}

} // namespace mini_lsm
