// lsm_engine.h
// -----------------------------------------------------------------------------
// LsmEngine — top-level facade tying together MemTable, SSTable, vLog,
// CompactionController, and the (S3) MVCC layer. The S0 skeleton implements
// just enough plumbing so that tests can drive put/get/scan through this
// single interface; the per-component unit tests exercise the same logic.
//
// Locking model (mini-lsm Chapter 1.6):
//   * `state_mutex_`   — short critical section to mutate the CoW state
//                        pointer + manifest + SST id allocator together.
//   * `state_` itself  — `std::shared_ptr<const LsmStorageState>` mutated by
//                        std::atomic_store-style swap. Readers call
//                        `snapshot()` which returns a shared_ptr copy and
//                        therefore NEVER block writers.
//
// S0 skeleton only implements the read/write API. Background flush thread,
// WAL, manifest, and compaction loop land in Stage S2.
// -----------------------------------------------------------------------------
#pragma once

#include "compaction.h"
#include "config.h"
#include "memtable.h"
#include "sstable.h"
#include "types.h"
#include "vlog.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <vector>

namespace mini_lsm {

// Snapshot of the engine state (CoW). The engine returns a shared_ptr to
// this struct to readers; writers replace it wholesale inside state_mutex_.
struct LsmStorageState {
    std::shared_ptr<MemTable>              active_memtable;
    std::vector<std::shared_ptr<MemTable>> immutable_memtables;   // [0] = newest frozen
    std::vector<std::uint64_t>             l0_sstables;            // newest first
    std::vector<std::vector<std::uint64_t>> levels;                // L1..LK
    std::unordered_map<std::uint64_t, std::shared_ptr<SSTable>> sstables;
};

// ---------------------------------------------------------------------------
// LsmEngine — the public surface that the tests drive.
// ---------------------------------------------------------------------------
class LsmEngine {
public:
    explicit LsmEngine(LsmOptions opts);
    ~LsmEngine();

    // Open `opts.base_dir` and recover prior state (S2). For S0 we simply
    // create directories and an empty in-memory state.
    Status open();

    // Idempotent close — joins background threads (S2+). S0 may print a
    // debug log but should not fail. Returned statuses:
    //   Status::OK unless open() was never called.
    Status close();

    // ---- Public read/write API ------------------------------------------
    // (mirrors the kvstore_api.h from the course project but returns Status
    //  and accepts std::string_view input rather than `(uint64_t, std::string)`)

    // Insert/Update a key-value pair. Tombstone convention: empty value ==
    // delete. The engine internally may flush this directly to the active
    // memtable OR transparently route through a MVCC transaction (S3+).
    Status put(KeyView key, ValueView value);

    // Lookup. Returns:
    //   std::nullopt  if key absent OR its latest version is a tombstone.
    //   value         otherwise.
    // Implementation suggestion (S0):
    //   1. snapshot() -> consult active memtable, then immutables[0..n-1]
    //   2. for each SST in L0 then levels ascending: run bloom+range check
    //   3. on first hit, return (tombstone => nullopt, otherwise value)
    std::optional<Value> get(KeyView key);

    // Logical delete. Equivalent to put(key, "").
    Status del(KeyView key) { return put(key, ValueView{}); }

    // Scan [lo, hi) // asc order; tombstones never appear in the output.
    // Writes results into `out` (clears it first); empty string at a key
    // position never appears (the tombstone filter strips it).
    Status scan(KeyView lo, KeyView hi,
                std::vector<std::pair<Key, Value>>& out);

    // ---- Maintenance hooks ----------------------------------------------
    // Manually trigger a freeze of the current memtable. Exposed so tests
    // can drive deterministic flows without waiting for size triggers.
    Status force_freeze_memtable();

    // Manually flush the oldest immutable memtable to disk and register
    // the resulting SSTable. Returns the new SSTable id (zero = none flushed).
    Status force_flush_next_imm_memtable(std::uint64_t& new_sst_id);

    // Manually trigger one compaction cycle (S2+). S0 returns kNotSupported.
    Status force_full_compaction() { return Status::NotSupported("S0 skeleton has no compaction yet"); }

    // Reset: drop all in-memory state AND remove all files under base_dir.
    // Useful for tests between scenarios. Caller is expected to call this
    // before each TEST_* body that needs a clean slate.
    Status reset();

    // ---- Test hooks -------------------------------------------------------
    // Read-only access to the current state pointer; readers MUST only call
    // `const` methods on the returned view.
    std::shared_ptr<const LsmStorageState> snapshot() const;

    // Allocate a fresh SST id (thread-safe; monotonic).
    std::uint64_t next_sst_id();

    // Underlying options for inspection by tests.
    const LsmOptions& options() const { return options_; }

    // Background flush thread activation (S2 lands the real impl; S0 exposes
    // start/stop interfaces with a no-op body so tests can detect "no flush").
    void start_flush_thread();
    void stop_flush_thread();

private:
    LsmOptions                               options_;
    std::shared_ptr<LsmStorageState>         state_;
    mutable std::mutex                       state_mutex_;
    std::atomic<std::uint64_t>              next_sst_id_{1};
    std::atomic<bool>                       stop_flag_{false};
    std::unique_ptr<CompactionController>   controller_;
    std::unique_ptr<CompactionExecutor>     executor_;
    VLog                                     vlog_;
    std::unique_ptr<std::thread>            flush_thread_;   // S2+
};

} // namespace mini_lsm