#include "sstable_builder.h"

#include <cstring>
#include <fstream>

namespace mini_lsm {
namespace {

void write_u16le(std::string& buf, std::uint16_t v) {
    buf.push_back(static_cast<char>(v & 0xFF));
    buf.push_back(static_cast<char>((v >> 8) & 0xFF));
}

void write_u32le(std::string& buf, std::uint32_t v) {
    buf.push_back(static_cast<char>(v & 0xFF));
    buf.push_back(static_cast<char>((v >> 8) & 0xFF));
    buf.push_back(static_cast<char>((v >> 16) & 0xFF));
    buf.push_back(static_cast<char>((v >> 24) & 0xFF));
}

void write_u64le(std::string& buf, std::uint64_t v) {
    for (int i = 0; i < 8; ++i) {
        buf.push_back(static_cast<char>((v >> (i * 8)) & 0xFF));
    }
}

} // anonymous namespace

SSTableBuilder::SSTableBuilder(std::size_t block_size, bool bloom_enabled)
    : block_size_(block_size), bloom_enabled_(bloom_enabled) {}

bool SSTableBuilder::add(KeyView key, ValueView value) {
    if (!entries_.empty() && !(last_key_ < key)) return false;
    if (entries_.empty()) first_key_ = Key{key};
    last_key_ = Key{key};
    ++num_entries_;
    total_data_bytes_ += key.size() + value.size();
    entries_.push_back({Key{key}, Value{value}});
    return true;
}

std::size_t SSTableBuilder::estimated_size_bytes() const {
    return total_data_bytes_;
}

Status SSTableBuilder::finish(std::uint64_t id, std::filesystem::path path,
                              std::unique_ptr<SSTable>& out) {
    std::ofstream f(path, std::ios::out | std::ios::binary);
    if (!f.is_open()) {
        return Status::IOError("cannot open " + path.string() + " for writing");
    }

    // Build block data, writing each block and recording its metadata.
    metas_.clear();
    std::size_t i = 0;
    while (i < entries_.size()) {
        std::size_t block_start = i;
        std::size_t block_bytes = 4; // count field

        while (i < entries_.size()) {
            auto entry_bytes = 2 + entries_[i].key.size() + 4 + entries_[i].value.size();
            if (block_bytes + entry_bytes > block_size_ && block_bytes > 4) break;
            block_bytes += entry_bytes;
            ++i;
        }

        std::string raw;
        write_u32le(raw, static_cast<std::uint32_t>(i - block_start));
        for (std::size_t k = block_start; k < i; ++k) {
            auto const& e = entries_[k];
            write_u16le(raw, static_cast<std::uint16_t>(e.key.size()));
            raw.append(e.key);
            write_u32le(raw, static_cast<std::uint32_t>(e.value.size()));
            raw.append(e.value);
        }
        BlockMeta meta;
        meta.offset = static_cast<std::uint32_t>(f.tellp());
        meta.num_entries = static_cast<std::uint32_t>(i - block_start);
        meta.first_key = entries_[block_start].key;
        meta.last_key = entries_[i - 1].key;

        f.write(raw.data(), static_cast<std::streamsize>(raw.size()));
        metas_.push_back(meta);
    }

    // Write meta blob
    auto meta_offset = static_cast<std::uint64_t>(f.tellp());
    std::string meta_buf;
    write_u32le(meta_buf, static_cast<std::uint32_t>(metas_.size()));
    for (auto const& m : metas_) {
        write_u32le(meta_buf, m.offset);
        write_u32le(meta_buf, m.num_entries);
        auto fklen = static_cast<std::uint16_t>(m.first_key.size());
        write_u16le(meta_buf, fklen);
        meta_buf.append(m.first_key);
        auto lklen = static_cast<std::uint16_t>(m.last_key.size());
        write_u16le(meta_buf, lklen);
        meta_buf.append(m.last_key);
    }
    f.write(meta_buf.data(), static_cast<std::streamsize>(meta_buf.size()));

    // Write bloom (optional, stub)
    auto bloom_offset = static_cast<std::uint64_t>(f.tellp());
    if (bloom_enabled_) {
        std::string bloom_buf;
        write_u32le(bloom_buf, 0); // empty bloom = 4 zero bytes
        f.write(bloom_buf.data(), static_cast<std::streamsize>(bloom_buf.size()));
    } else {
        bloom_offset = 0;
    }

    // Write footer
    std::string footer;
    write_u64le(footer, meta_offset);
    write_u64le(footer, bloom_offset);
    write_u64le(footer, SSTable::kMagic);
    f.write(footer.data(), static_cast<std::streamsize>(footer.size()));
    f.close();

    return SSTable::open(id, path, out);
}

} // namespace mini_lsm
