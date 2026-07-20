// compaction.h
// -----------------------------------------------------------------------------
// CompactionController — picks compaction tasks; CompactionExecutor — runs
// the I/O and rewrites SST lists. S0 only declares the abstract scaffold so
// the engine's `trigger_compaction` / `force_full_compaction` have a target
// type to compile against.
//
// Design rationale (mini-lsm Week 2 §Compact.rs):
//   mini-lsm uses a single Rust enum `CompactionTask` with five variants
//   (Simple/Tiered/Leveled/ForceFull) and dispatches via `match`. In C++ we
//   reach for the equivalent: `std::variant<...>` + `std::visit`. S0 keeps
//   the scaffold concrete-by-virtual instead, which makes adding a new mock
//   implementation for tests trivial. S2 will switch to variant-style for
//   the production code paths.
//
// Implementation suggestion (S0):
//   * Defaults are no-ops; each returns `Status::NotSupported("TODO: ...")`.
//   * The `CompactionController` default `pick_task` returns std::nullopt so
//     the engine's compaction loop is a busy-poll with no work to do.
//   * See `tests/test_compaction.cpp` for the contract that the S2 graduated
//     implementation needs to satisfy.
// -----------------------------------------------------------------------------
#pragma once

#include "sstable.h"
#include "types.h"
#include <memory>
#include <optional>
#include <vector>

namespace mini_lsm {

class LsmEngine;       // forward decl

// Snowflake-style sample compaction task structure. Fields vary widely across
// SimpleLeveled / Tiered / Leveled strategies; in Stage S2 we'll replace this
// with a `std::variant<Simple, Tiered, Leveled, ForceFull>` and `std::visit`.
struct CompactionTask {
    enum class Kind {
        kSimpleLeveled,
        kTiered,
        kLeveled,
        kForceFull,
        kNone,
    };
    Kind                       kind           = Kind::kNone;
    std::uint32_t              upper_level    = 0;          // Level 0 means L0
    std::vector<std::uint64_t> upper_ssts;
    std::uint32_t              lower_level    = 1;
    std::vector<std::uint64_t> lower_ssts;
    bool                       is_bottom_level = false;
};

class CompactionController {
public:
    virtual ~CompactionController() = default;

    // Inspect the engine's current state and decide whether to compact.
    // Returning std::nullopt means "no work right now" — the loop will poll
    // again after a tick (see LsmEngine::compaction_loop).
    virtual std::optional<CompactionTask> pick_task(const LsmEngine& engine) const = 0;

    // Whether freshly-flushed MemTables should land in L0 (true) or be
    // directly turned into a new tiered run (false; mini-lsm's tiered strategy).
    virtual bool flush_to_l0() const { return true; }
};

class CompactionExecutor {
public:
    virtual ~CompactionExecutor() = default;

    // Perform the sorted merge over `task`'s inputs and produce a list of
    // new SSTable ids that the engine must register. The caller is
    // responsible for applying the result to the manifest (S2) and deleting
    // the old SSTables.
    virtual Status execute(const CompactionTask& task,
                           LsmEngine& engine,
                           std::vector<std::uint64_t>& new_sst_ids) = 0;
};

// S0 stubs ------------------------------------------------------------------
class NoCompactionController : public CompactionController {
public:
    std::optional<CompactionTask> pick_task(const LsmEngine&) const override {
        return std::nullopt;
    }
};

class NoCompactionExecutor : public CompactionExecutor {
public:
    Status execute(const CompactionTask&, LsmEngine&,
                   std::vector<std::uint64_t>&) override;
};

} // namespace mini_lsm