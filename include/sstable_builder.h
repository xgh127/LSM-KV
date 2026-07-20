// sstable_builder.h
// -----------------------------------------------------------------------------
// SSTableBuilder — accumulates (key, value) pairs and flushes them to disk
// as a multi-block SSTable whose layout is defined in sstable.h's header
// comment.
//
// S0 contract:
//   * The builder is told when to finish a block (the engine calls
//     `add`(key, value) repeatedly and checks `estimated_size_bytes()`).
//   * Suggested usage pattern (pseudo):
//        builder.add(k1, v1); builder.add(k2, v2);
//        if (builder.estimated_size_bytes() >= options.target_sst_size) ...
//        ...
//        builder.finish(...)   // also serializes meta+bloom+footer, closes file
//   * Keys MUST be inserted in strictly ascending order (no equal keys
//     within a single SST). Users that allow overwrite produce a new version
//     per MemTable; the LSM-tree layer handles dedup at read time.
//
// Implementation suggestion (S0):
//   For the skeleton, `add` may just push (k, v) into an in-memory vector;
//   `finish` may write a single concatenated block. Format precision is a
//   Stage S1 concern — S0 asserts:
//     - num_entries() matches
//     - finish() returns no IOError
//     - first_key()/last_key() visible AFTER `finish` (cached from adds)
// -----------------------------------------------------------------------------
#pragma once

#include "sstable.h"
#include "types.h"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace mini_lsm {

class SSTableBuilder {
public:
    explicit SSTableBuilder(std::size_t block_size, bool bloom_enabled);

    // Append a (key, value) pair. Keys MUST be inserted in ascending order.
    // Returns true if accepted, false on ordering violation / capacity.
    // Implementation suggestion:
    //   if (!data_.empty() && key <= last_key_) return false;
    //   entries_.push_back({key, value});
    //   last_key_ = key;
    //   return true;
    bool add(KeyView key, ValueView value);

    // Current on-disk estimated size = sum of all data plus a header.
    std::size_t estimated_size_bytes() const { return data_.size(); }

    bool is_empty() const { return data_.empty(); }

    KeyView first_key() const { return first_key_; }
    KeyView last_key()  const { return last_key_; }

    std::size_t num_entries() const { return num_entries_; }

    // Finalize and write to `path`. On success, `out` becomes an opened
    // SSTable (same as if calling SSTable::open on the freshly written file).
    // Implementation suggestion (S1):
    //   1. iterate entries_, chunk into blocks of `block_size_`
    //   2. for each block: encode + record BlockMeta + write to file
    //   3. write meta blob, optional bloom, footer
    //   4. re-open via SSTable::open for `out`
    Status finish(std::uint64_t id, std::filesystem::path path,
                  std::unique_ptr<SSTable>& out);

private:
    std::size_t      block_size_;
    bool             bloom_enabled_;
    std::string      data_;            // accumulated serialized bytes so far
    Key              first_key_;
    Key              last_key_;
    std::size_t      num_entries_ = 0;
    std::vector<BlockMeta> metas_;
    std::vector<std::uint32_t> key_hashes_;   // for bloom (k bits/key)
};

} // namespace mini_lsm