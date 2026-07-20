// iterator.h
// -----------------------------------------------------------------------------
// StorageIterator — the abstract cursor over an ordered key space.
//
// All LSM readers (MemTable scan, SST iteration, Merge_COMBINE, etc.) are
// unified by this base class. S0 keeps it intentionally tiny; S1 will add
// `seek_to_first/seek_to_key`; S3 (MVCC) will add a `KeyType` carrying ts.
//
// Why a virtual base class instead of Rust's `trait`?
//   mini-lsm uses `Box<dyn StorageIterator>` only sparingly inside collection
//   fields (e.g. MergeIterator), preferring static generics for the rest.
//   In C++ the equivalent trade-off maps to: virtual base in the public
//   iterator API of the engine, *plus* an optional CRTP static-dispatch
//   version for hot MergeIterator inner loops (Stage S4 bonus).
//
// Implementation suggestion (S0 / S1):
//   Subclass once per component (MemTableIterator, BlockIterator, ...).
//   Keep `num_active_iterators()` returning 1 by default — override later.
// -----------------------------------------------------------------------------
#pragma once

#include "types.h"

namespace mini_lsm {

class StorageIterator {
public:
    virtual ~StorageIterator() = default;

    // Is the iterator currently positioned at a valid entry?
    // Returns false after end-of-stream or after a seek failure.
    virtual bool is_valid() const = 0;

    // Key / value at the current position. Both views are only valid while
    // the iterator stays at the same position AND the underlying storage
    // stays alive (e.g. the MemTable or SST is not destroyed).
    // Calling key()/value() when !is_valid() is undefined behaviour — guard.
    virtual KeyView   key()   const = 0;
    virtual ValueView value() const = 0;

    // Advance to the next entry. Returns Status::OK() generally; on I/O
    // backed iterators (SST), it returns the underlying I/O failure, in
    // which case is_valid() becomes false and subsequent next() calls
    // keep returning the same error (this is the "fused" semantics
    // referenced in mini-lsm `FusedIterator`).
    virtual Status next() = 0;

    // Cost metrics for iterator composition (mini-lsm Week 1 Day 6).
    // Default = 1; MergeIterator / TwoMergeIterator override as the sum
    // of their children. Used by tests to verify SST filtering on scans.
    virtual std::size_t num_active_iterators() const { return 1; }
};

} // namespace mini_lsm