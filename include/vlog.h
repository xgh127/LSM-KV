// vlog.h
// -----------------------------------------------------------------------------
// vLog (Value Log) — the WiscKey separation of large values out of the LSM
// tree. The course PDF mandates this; mini-lsm doesn't include it.
//
// Idea (WiscKey / Bitcast / RocksDB BlobDB):
//   * Large values are written appended to a separate append-only file (the
//     "vlog").
//   * The LSM-tree stores the value's position (offset + length) inside the
//     vlog instead of the value itself, keeping MemTable/SST density high.
//   * `gc(chunk_size)` reclaims space from the tail by migrating still-live
//     values to the head and discarding dead ones.
//
// Layout of a vLog record (header fixed-size):
//   | magic (1B) | key_len (2B) | key | value_len (4B) | value | CRC32 (4B) |
//
// The S0 skeleton provides the API plus a no-op `gc` so tests can assert:
//   * vlog file is created on demand (temp dir supplied by the test harness)
//   * append_to_tail returns a handle whose offset is monotonically increasing
//   * read_at(offset, len) returns the same bytes that were written
//   * no I/O errors in the happy path
//
// Implementation suggestion (S0): std::ofstream (append, binary) + std::ifstream
// for reading. Keep a small in-RAM index of (key -> latest offset) so get(key)
// is O(1) for the tests; the production path goes through the LSM-tree
// metadata rather than this small index.
// -----------------------------------------------------------------------------
#pragma once

#include "types.h"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

namespace mini_lsm {

// Persistent handle pointing at a value location inside the vLog file.
struct VLogHandle {
    std::uint64_t offset = 0;
    std::uint32_t length = 0;
};

class VLog {
public:
    VLog() = default;

    // Tests open by path. If `create_if_missing` is true, missing parents
    // are created.
    // Implementation suggestion:
    //   open ofstream(path, ios::out | ios::binary | ios::app)
    //   store path_ for read-back
    Status open(std::filesystem::path path, bool create_if_missing);

    // Append a (key, value) pair to the tail of the vlog and return its
    // handle. The write must be atomic at the file level (O_APPEND) so that
    // concurrent writes from one writer can be interleaved without belts.
    // Implementation suggestion: build a buffer [header | key | value | crc],
    //   ofstream_.write() — file pos is the requested `handle.offset`.
    Status append(KeyView key, ValueView value, VLogHandle& out);

    // Read `length` bytes starting at `offset`. The returned bytes MUST
    // equal the value component of the record that produced the handle.
    // Implementation suggestion: ifstream_.seekg(offset); read(length).
    Status read_at(VLogHandle h, std::string& out) const;

    // Reclaim space from the tail of the vlog by moving still-live values
    // (those whose keys are still valid in the LSM-tree) towards the head
    // and discarding dead ones. `chunk_size` is the minimum number of bytes
    // the GC pass should reclaim (the name matches the course PDF
    // `gc(uint64_t chunk_size)`). The S0 implementation may be a no-op that
    // simply returns Status::OK() with no bytes reclaimed; tests will
    // exercise a fake GC that does the bookkeeping externally.
    // Real implementation (S2+/bonus) — see WiscKey paper §4.4:
    //   1. scan vlog entries from head until chunk_size bytes processed
    //   2. for each entry, ask LSM-tree if key is still alive; if yes,
    //      re-append to tail and update LSM-tree's stored handle
    //   3. truncate / advance the head pointer past the scanned chunk
    Status gc(std::uint64_t chunk_size, std::uint64_t& reclaimed);

    // Returns the current file length (next offset).
    std::uint64_t size_bytes() const;

    std::filesystem::path const& path() const { return path_; }

private:
    std::filesystem::path path_;
    std::ofstream          out_;
    std::ifstream          in_;
    std::uint64_t          next_offset_ = 0;
};

} // namespace mini_lsm