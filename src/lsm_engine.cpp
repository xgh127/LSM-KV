// lsm_engine.cpp
// -----------------------------------------------------------------------------
// S0 LsmEngine implementation. The contract is intentionally minimal: only
// the public put/get/del/scan/force_freeze/force_flush/reset/run scaffolding
// exists — the bodies are stubs so tests fail RED, never crash.
//
// Locking model recap (mirrors mini-lsm `lsm_storage.rs`):
//   * state_mutex_       — serialize writes that want a consistent snapshot
//                          across freeze + flush + compaction state updates.
//   * state_             — CoW `shared_ptr<const LsmStorageState>`; readers
//                          take a copy and the writer replaces the whole ptr
//                          under state_mutex_.
// -----------------------------------------------------------------------------
#include "lsm_engine.h"
#include "sstable_builder.h"

#include <cstdio>
#include <filesystem>
#include <thread>

namespace mini_lsm {

LsmEngine::LsmEngine(LsmOptions opts) : options_(std::move(opts)) {
    state_ = std::make_shared<LsmStorageState>();
    state_->active_memtable = std::make_shared<MemTable>(next_sst_id_.fetch_add(1));
    controller_ = std::make_unique<NoCompactionController>();
    executor_   = std::make_unique<NoCompactionExecutor>();
}

LsmEngine::~LsmEngine() {
    // Idempotent close — S2 actually joins the flush thread here.
    stop_flush_thread();
}

Status LsmEngine::open() {
    std::error_code ec;
    std::filesystem::create_directories(options_.base_dir, ec);
    if (ec) {
        return Status::IOError(std::string{"create_directories: "} + ec.message());
    }
    return Status::OK();
}

Status LsmEngine::close() {
    stop_flush_thread();
    return Status::OK();
}

// ---- Public read/write API ------------------------------------------------

Status LsmEngine::put(KeyView key, ValueView value) {
    auto snap = snapshot();
    return snap->active_memtable->put(key, value);
}

std::optional<Value> LsmEngine::get(KeyView key) {
    auto snap = snapshot();
    // Check memtables (newest first)
    auto v = snap->active_memtable->get(key);
    if (v.has_value()) return v;
    for (auto& im : snap->immutable_memtables) {
        v = im->get(key);
        if (v.has_value()) return v;
    }
    // Check L0 SSTables (newest first)
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
        std::size_t count = 0;
        std::vector<std::pair<Key, Value>> entries;
        // inline decode
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
        count = read_u32(raw.data());
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
                if (val.empty()) return std::nullopt; // tombstone
                return val;
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

// ---- Maintenance hooks ----------------------------------------------------

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

    // Build SSTable from immutable memtable
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

    state_->immutable_memtables.pop_back();
    state_->l0_sstables.insert(state_->l0_sstables.begin(), new_sst_id);
    state_->sstables[new_sst_id] = std::move(tbl);
    return Status::OK();
}

Status LsmEngine::reset() {
    // Drop in-memory + on-disk state; recreate empty state.
    stop_flush_thread();
    std::error_code ec;
    std::filesystem::remove_all(options_.base_dir, ec);
    // Ignore ec: directory may not exist.
    std::filesystem::create_directories(options_.base_dir, ec);

    auto fresh = std::make_shared<LsmStorageState>();
    fresh->active_memtable = std::make_shared<MemTable>(next_sst_id_.fetch_add(1));
    {
        std::lock_guard<std::mutex> g(state_mutex_);
        state_ = std::move(fresh);
    }
    return Status::OK();
}

// ---- Test hooks -----------------------------------------------------------

std::shared_ptr<const LsmStorageState> LsmEngine::snapshot() const {
    // C++20 atomic_load on shared_ptr is deprecated; use a mutex instead.
    std::lock_guard<std::mutex> g(state_mutex_);
    return std::const_pointer_cast<const LsmStorageState>(state_);
}

std::uint64_t LsmEngine::next_sst_id() {
    return next_sst_id_.fetch_add(1);
}

void LsmEngine::start_flush_thread() {
    // S0 stub for S2 implementation. See PLAN.md §4 Stage S2.
    if (flush_thread_) return;
    stop_flag_ = false;
    flush_thread_ = std::make_unique<std::thread>([this] {
        // TODO(S2): 50 ms tick loop poll on imm count >= num_memtable_limit.
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