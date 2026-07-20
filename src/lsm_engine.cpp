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

Status LsmEngine::put(KeyView /*key*/, ValueView /*value*/) {
    // TODO(S1): snapshot() -> active_memtable->put(key, value); check size
    //      and trigger freeze if over `memtable_target_size`.
    return Status::OK();
}

std::optional<Value> LsmEngine::get(KeyView /*key*/) {
    // TODO(S1): consult memtable -> immutable[0..n-1] -> SSTables L0..Lk.
    //  Always returns std::nullopt until something is implemented.
    return std::nullopt;
}

Status LsmEngine::scan(KeyView /*lo*/, KeyView /*hi*/,
                       std::vector<std::pair<Key, Value>>& /*out*/) {
    // TODO(S1): merge iterators over memtable + sstables, apply tombstone
    //  filter, return ordered non-tombstone KV pairs.
    return Status::OK();
}

// ---- Maintenance hooks ----------------------------------------------------

Status LsmEngine::force_freeze_memtable() {
    // TODO(S1): under state_mutex_:
    //   1. mark active_memtable->freeze()
    //   2. push front onto immutable_memtables
    //   3. install fresh active_memtable
    return Status::OK();
}

Status LsmEngine::force_flush_next_imm_memtable(std::uint64_t& new_sst_id) {
    // TODO(S1+): under state_mutex_, take oldest immutable, flush via
    //   SSTableBuilder::finish, insert into l0_sstables front, pop immutable.
    new_sst_id = 0;
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