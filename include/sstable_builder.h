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

    bool add(KeyView key, ValueView value);

    std::size_t estimated_size_bytes() const;

    bool is_empty() const { return entries_.empty(); }

    KeyView first_key() const { return first_key_; }
    KeyView last_key()  const { return last_key_; }

    std::size_t num_entries() const { return num_entries_; }

    Status finish(std::uint64_t id, std::filesystem::path path,
                  std::unique_ptr<SSTable>& out);

private:
    struct Entry { Key key; Value value; };

    std::size_t      block_size_;
    bool             bloom_enabled_;
    std::vector<Entry> entries_;
    Key              first_key_;
    Key              last_key_;
    std::size_t      num_entries_ = 0;
    std::uint64_t    total_data_bytes_ = 0;
    std::vector<BlockMeta> metas_;
    std::vector<std::uint32_t> key_hashes_;
};

} // namespace mini_lsm