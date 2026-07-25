#include "lsm_engine.h"
#include "sstable_builder.h"

#include <algorithm>
#include <cstdio>
#include <filesystem>

namespace mini_lsm {

LsmEngine::LsmEngine(LsmOptions opts) : options_(std::move(opts)) {
    state_ = std::make_shared<LsmStorageState>();
    state_->active_memtable = std::make_shared<MemTable>(next_sst_id_.fetch_add(1));
    controller_ = std::make_unique<FullCompactionController>(
        options_.level0_file_num_trigger);
    executor_   = std::make_unique<FullCompactionExecutor>();
}

LsmEngine::~LsmEngine() {
    close();
}

Status LsmEngine::open() {
    std::error_code ec;
    std::filesystem::create_directories(options_.base_dir, ec);
    if (ec) {
        return Status::IOError(std::string{"create_directories: "} + ec.message());
    }

    // Open Manifest
    auto manifest_path = options_.base_dir / "MANIFEST";
    auto s = manifest_.open(manifest_path);
    if (!s.ok()) return s;

    // Recover level structure from Manifest
    std::unordered_map<std::uint64_t, std::uint32_t> sst_levels;
    s = manifest_.recover(sst_levels);
    if (!s.ok()) return s;

    // Rebuild state from manifest
    for (auto& [sst_id, level] : sst_levels) {
        auto sst_path = options_.base_dir / (std::to_string(sst_id) + ".sst");
        if (!std::filesystem::exists(sst_path)) continue;
        std::unique_ptr<SSTable> tbl;
        auto open_s = SSTable::open(sst_id, sst_path, tbl);
        if (!open_s.ok()) continue;
        auto shared = std::shared_ptr<SSTable>(std::move(tbl));
        state_->sstables[sst_id] = shared;
        if (level == 0) {
            state_->l0_sstables.push_back(sst_id);
        } else {
            if (static_cast<std::size_t>(level) >= state_->levels.size()) {
                state_->levels.resize(level + 1);
            }
            state_->levels[level].push_back(sst_id);
        }
    }

    // Open WAL
    auto wal_path = options_.base_dir / "WAL";
    wal_ = WAL{};  // reset
    s = wal_.open(wal_path);
    if (!s.ok()) return s;

    // Recover MemTable from WAL
    std::vector<std::pair<Key, Value>> wal_entries;
    s = wal_.recover(wal_entries);
    if (!s.ok()) return s;
    for (auto& [k, v] : wal_entries) {
        state_->active_memtable->put(k, v);
    }

    return Status::OK();
}

Status LsmEngine::close() {
    stop_flush_thread();
    wal_.close();
    return Status::OK();
}

Status LsmEngine::put(KeyView key, ValueView value) {
    // Write WAL first (crash safety)
    auto s = wal_.append(key, value);
    if (!s.ok()) return s;
    // Then write to MemTable
    auto snap = snapshot();
    return snap->active_memtable->put(key, value);
}

std::optional<Value> LsmEngine::get(KeyView key) {
    auto snap = snapshot();
    auto v = snap->active_memtable->get(key);
    if (v.has_value()) return v;
    for (auto& im : snap->immutable_memtables) {
        v = im->get(key);
        if (v.has_value()) return v;
    }
    for (auto sst_id : snap->l0_sstables) {
        auto it = snap->sstables.find(sst_id);
        if (it == snap->sstables.end()) continue;
        auto const& tbl = it->second;
        if (!tbl->may_contain(key)) continue;
        auto blk = tbl->find_block_idx(key);
        if (blk >= tbl->num_blocks()) continue;
        std::string raw;
        auto s = tbl->read_block(blk, raw);
        if (!s.ok()) continue;
        if (raw.size() < 4) continue;
        auto read_u32 = [](const char* p) -> std::uint32_t {
            return static_cast<unsigned char>(p[0]) |
                   (static_cast<unsigned char>(p[1]) << 8) |
                   (static_cast<unsigned char>(p[2]) << 16) |
                   (static_cast<unsigned char>(p[3]) << 24);
        };
        auto read_u16 = [](const char* p) -> std::uint16_t {
            return static_cast<unsigned char>(p[0]) |
                   (static_cast<unsigned char>(p[1]) << 8);
        };
        auto count = read_u32(raw.data());
        std::size_t pos = 4;
        for (std::size_t j = 0; j < count; ++j) {
            if (pos + 2 > raw.size()) break;
            auto klen = read_u16(raw.data() + pos);
            pos += 2;
            if (pos + klen > raw.size()) break;
            Key k{raw.substr(pos, klen)};
            pos += klen;
            if (pos + 4 > raw.size()) break;
            auto vlen = read_u32(raw.data() + pos);
            pos += 4;
            if (pos + vlen > raw.size()) break;
            Value val{raw.substr(pos, vlen)};
            pos += vlen;
            if (k == key) {
                return val.empty() ? std::nullopt : std::make_optional(val);
            }
        }
    }
    for (auto const& level_ssts : snap->levels) {
        for (auto sst_id : level_ssts) {
            auto it = snap->sstables.find(sst_id);
            if (it == snap->sstables.end()) continue;
            auto const& tbl = it->second;
            if (!tbl->may_contain(key)) continue;
            auto blk = tbl->find_block_idx(key);
            if (blk >= tbl->num_blocks()) continue;
            std::string raw;
            auto s = tbl->read_block(blk, raw);
            if (!s.ok()) continue;
            if (raw.size() < 4) continue;
            auto read_u32 = [](const char* p) -> std::uint32_t {
                return static_cast<unsigned char>(p[0]) |
                       (static_cast<unsigned char>(p[1]) << 8) |
                       (static_cast<unsigned char>(p[2]) << 16) |
                       (static_cast<unsigned char>(p[3]) << 24);
            };
            auto read_u16 = [](const char* p) -> std::uint16_t {
                return static_cast<unsigned char>(p[0]) |
                       (static_cast<unsigned char>(p[1]) << 8);
            };
            auto count = read_u32(raw.data());
            std::size_t pos = 4;
            for (std::size_t j = 0; j < count; ++j) {
                if (pos + 2 > raw.size()) break;
                auto klen = read_u16(raw.data() + pos);
                pos += 2;
                if (pos + klen > raw.size()) break;
                Key k{raw.substr(pos, klen)};
                pos += klen;
                if (pos + 4 > raw.size()) break;
                auto vlen = read_u32(raw.data() + pos);
                pos += 4;
                if (pos + vlen > raw.size()) break;
                Value val{raw.substr(pos, vlen)};
                pos += vlen;
                if (k == key) {
                    return val.empty() ? std::nullopt : std::make_optional(val);
                }
            }
        }
    }
    return std::nullopt;
}

Status LsmEngine::scan(KeyView lo, KeyView hi,
                       std::vector<std::pair<Key, Value>>& out) {
    auto snap = snapshot();
    out.clear();
    auto it = snap->active_memtable->scan(lo, hi);
    while (it->is_valid()) {
        out.emplace_back(Key{it->key()}, Value{it->value()});
        it->next();
    }
    return Status::OK();
}

Status LsmEngine::force_freeze_memtable() {
    std::lock_guard<std::mutex> g(state_mutex_);
    auto old = state_->active_memtable;
    old->freeze();
    state_->immutable_memtables.insert(state_->immutable_memtables.begin(), old);
    state_->active_memtable = std::make_shared<MemTable>(next_sst_id_.fetch_add(1));
    return Status::OK();
}

Status LsmEngine::force_flush_next_imm_memtable(std::uint64_t& new_sst_id) {
    std::lock_guard<std::mutex> g(state_mutex_);
    if (state_->immutable_memtables.empty()) {
        new_sst_id = 0;
        return Status::OK();
    }
    auto back = state_->immutable_memtables.back();
    new_sst_id = back->id();

    SSTableBuilder builder(options_.block_size_bytes, options_.bloom_filter_enabled);
    auto it = back->scan("", "\xff\xff\xff\xff\xff");
    while (it->is_valid()) {
        builder.add(it->key(), it->value());
        it->next();
    }

    auto sst_path = options_.base_dir / (std::to_string(new_sst_id) + ".sst");
    std::unique_ptr<SSTable> tbl;
    auto s = builder.finish(new_sst_id, sst_path, tbl);
    if (!s.ok()) return s;

    // Manifest: record add
    ManifestRecord rec;
    rec.type = ManifestRecord::kAddSst;
    rec.sst_id = new_sst_id;
    rec.level = 0;
    manifest_.append(rec);

    state_->immutable_memtables.pop_back();
    state_->l0_sstables.insert(state_->l0_sstables.begin(), new_sst_id);
    state_->sstables[new_sst_id] = std::move(tbl);
    return Status::OK();
}

Status LsmEngine::force_full_compaction() {
    auto task_opt = controller_->pick_task(*this);
    if (!task_opt.has_value()) {
        return Status::OK();  // nothing to compact
    }

    auto task = *task_opt;
    auto old_sst_ids = task.upper_ssts;

    // Execute compaction WITHOUT state_mutex_ (executor calls snapshot())
    std::vector<std::uint64_t> new_sst_ids;
    auto s = executor_->execute(task, *this, new_sst_ids);
    if (!s.ok()) return s;

    std::lock_guard<std::mutex> g(state_mutex_);

    // Update manifest: remove old L0 SSTs, add new L1 SST
    for (auto id : old_sst_ids) {
        ManifestRecord rec;
        rec.type = ManifestRecord::kRemoveSst;
        rec.sst_id = id;
        manifest_.append(rec);
    }
    for (auto id : new_sst_ids) {
        ManifestRecord rec;
        rec.type = ManifestRecord::kAddSst;
        rec.sst_id = id;
        rec.level = static_cast<std::uint32_t>(task.lower_level);
        manifest_.append(rec);
    }

    // Update state
    for (auto id : old_sst_ids) {
        auto it = std::find(state_->l0_sstables.begin(),
                            state_->l0_sstables.end(), id);
        if (it != state_->l0_sstables.end()) {
            state_->l0_sstables.erase(it);
        }
        state_->sstables.erase(id);
    }
    // Add new SSTs to the target level
    if (static_cast<std::size_t>(task.lower_level) >= state_->levels.size()) {
        state_->levels.resize(task.lower_level + 1);
    }
    for (auto id : new_sst_ids) {
        state_->levels[task.lower_level].push_back(id);
        auto sst_path = options_.base_dir / (std::to_string(id) + ".sst");
        std::unique_ptr<SSTable> tbl;
        auto open_s = SSTable::open(id, sst_path, tbl);
        if (open_s.ok()) {
            state_->sstables[id] = std::shared_ptr<SSTable>(std::move(tbl));
        }
    }

    return Status::OK();
}

Status LsmEngine::reset() {
    stop_flush_thread();
    wal_.close();
    // Remove WAL file (start fresh)
    std::error_code ec;
    std::filesystem::remove(options_.base_dir / "WAL", ec);
    std::filesystem::remove(options_.base_dir / "MANIFEST", ec);
    std::filesystem::remove_all(options_.base_dir, ec);
    std::filesystem::create_directories(options_.base_dir, ec);

    auto fresh = std::make_shared<LsmStorageState>();
    fresh->active_memtable = std::make_shared<MemTable>(next_sst_id_.fetch_add(1));
    {
        std::lock_guard<std::mutex> g(state_mutex_);
        state_ = std::move(fresh);
    }
    // Re-open WAL
    wal_ = WAL{};
    wal_.open(options_.base_dir / "WAL");
    // Re-open Manifest
    manifest_ = Manifest{};
    manifest_.open(options_.base_dir / "MANIFEST");
    return Status::OK();
}

std::shared_ptr<const LsmStorageState> LsmEngine::snapshot() const {
    std::lock_guard<std::mutex> g(state_mutex_);
    return std::const_pointer_cast<const LsmStorageState>(state_);
}

std::uint64_t LsmEngine::next_sst_id() {
    return next_sst_id_.fetch_add(1);
}

void LsmEngine::start_flush_thread() {
    if (flush_thread_) return;
    stop_flag_ = false;
    flush_thread_ = std::make_unique<std::thread>([this] {
        while (!stop_flag_.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
}

void LsmEngine::stop_flush_thread() {
    if (!flush_thread_) return;
    stop_flag_ = true;
    if (flush_thread_->joinable()) flush_thread_->join();
    flush_thread_.reset();
}

} // namespace mini_lsm
