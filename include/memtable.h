// memtable.h
// -----------------------------------------------------------------------------
// MemTable — the in-memory, sorted, mutable write buffer sitting in front of
// the LSM-tree. The `put` operation only requires `&self` (lock-free in a real
// skip-list implementation); `scan` returns a `MemTableIterator` that is
// invalidated if the MemTable is mutated mid-iteration.
//
// S0 skeleton contract:
//   * The "functional" surface (put/get/delete/scan/approximate_size) is fully
//     declared; only the bodies are left empty. The tests assert simple
//     invariants (put→get round-trip, delete tombstone logic, scan ordering,
//     freeze semantics) and will FAIL until you implement the bodies.
//   * The storage layout you pick is up to you. We suggest:
//       start with std::map<Key, Value>  (red-black tree; simplest correctness)
//       replace with a lock-free skiplist as Stage S4 bonus
//   * Capacity / freeze logic is a property of the *engine*, not the memtable
//     itself — see LsmEngine::force_freeze_memtable.
//
// Tombstone convention (whole-engine):
//   put(key, "") == delete(key). An empty value entry represents a tombstone
//   and is never returned to the user. The get/scan paths must filter it.
// -----------------------------------------------------------------------------
#pragma once

#include "iterator.h"
#include "types.h"
#include <atomic>
#include <map>
#include <memory>
#include <optional>

namespace mini_lsm {

class SSTableBuilder; // forward decl

// ---------------------------------------------------------------------------
// MemTable
// ---------------------------------------------------------------------------
class MemTable {
public:
    explicit MemTable(std::uint64_t id) : id_(id) {}

    // Returns the id assigned at construction (used to name SST files / WAL).
    std::uint64_t id() const { return id_; }

   
    Status put(KeyView key, ValueView value);

    // Single-key lookup. Returns:
    //   std::nullopt         if key is absent OR has a tombstone
    //   "real" value         otherwise
    // Implementation suggestion: map_.find -> tombstone check -> return.
    std::optional<Value> get(KeyView key) const;

    // Logical delete — equivalent to put(key, "").
    Status del(KeyView key) { return put(key, ValueView{}); }

    // Forward iteration in [lo, hi) using inclusive lower / exclusive upper
    // bounds (this is exactly the same wording as the LSM-c++ kvstore_api.h
    // `scan` signature translated to string-bounded keys).
    //
    // Returns a fresh, self-owned StorageIterator positioned at the first
    // entry with key >= lo. Caller advances until key >= hi.
    //
    // Implementation suggestion: hold a shared_ptr<const MemTable> inside the
    // returned iterator so the iterator outlives the skiplist (mirrors
    // `ouroboros::ouroboros` pattern in Rust `MemTableIterator`).
    std::unique_ptr<StorageIterator> scan(KeyView lo, KeyView hi) const;

    // Drain the memtable's contents into the builder in sorted order.
    // Called by `LsmEngine::force_flush_next_imm_memtable` during flush.
    // Implementation suggestion:
    //   for (auto& [k, v] : map_) builder.add(k, v);
    //   return Status::OK();
    Status flush_to(SSTableBuilder& builder) const;

    // Approximate size in bytes; used by the engine to decide freeze.
    std::size_t approximate_size() const { return size_.load(); }

    // Mark this memtable as immutable (no further puts accepted). Same call
    // returns any pre-existing State in case tests want to assert atomicity.
    void freeze() { frozen_ = true; }
    bool is_frozen() const { return frozen_; }

private:
    std::uint64_t                         id_;          
    std::map<Key, Value, std::less<>>     map_;       // transparent comparator (C++20)
    std::atomic<std::size_t>             size_{0};    //为了线程安全，将size_声明为std::atomic<std::size_t>，即原子变量
    bool                                 frozen_ = false;
};

} // namespace mini_lsm