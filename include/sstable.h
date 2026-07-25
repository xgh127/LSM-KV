// sstable.h
// -----------------------------------------------------------------------------
// SSTable (Sorted String Table) — the immutable on-disk container produced
// by SSTableBuilder. Each SSTable consists of:
//   * N data blocks        (block_size_bytes each)
//   * BlockMeta[]          (per-block first/last key + offsets)
//   * Bloom filter         (optional, kBitsPerKey specified in LsmOptions)
//   * Footer                (offset to meta + offset to bloom + magic)
//
// The S0 skeleton leaves I/O abstracted behind a path so the engine layer can
// rebind it in S4 to an AsyncIo backend (io_uring / IOCP) without touching
// the format-level code. Tests only assert on BlockMeta contents and encoded
// byte length — they do not stress format compatibility with RocksDB yet.
//
// Layout (byte stream, little-endian unless noted):
//   [ block 0 ][ block 1 ] ... [ block N-1 ]
//   [ meta blob: |u32 count| BlockMeta[count] | ]
//   [ optional bloom blob: |u32 bytes| bloom[]| ]
//   [ fixed footer: |u64 meta_off| |u64 bloom_off| |u64 magic| ]
//
// All offsets are relative to file start.
// -----------------------------------------------------------------------------
#pragma once

#include "iterator.h"
#include "types.h"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace mini_lsm {

// ---------------------------------------------------------------------------
// BlockMeta
// ---------------------------------------------------------------------------
struct BlockMeta {
    std::uint32_t offset     = 0;   // absolute file offset of the block
    std::uint32_t num_entries = 0;  // entries inside the block (for stats)
    Key           first_key;        // smallest key inside the block
    Key           last_key;         // largest key inside the block
};

// ---------------------------------------------------------------------------
// SsTable
// ---------------------------------------------------------------------------
class SSTable {
public:
    SSTable(std::uint64_t id, std::filesystem::path path);

    std::uint64_t id()   const { return id_; }
    std::filesystem::path const& path() const { return path_; }

    // Number of blocks in this table.
    std::size_t num_blocks() const { return block_metas_.size(); }

    // View over the in-memory metadata. Empty until `open()` is implemented.
    std::vector<BlockMeta> const& block_metas() const { return block_metas_; }

    // Returns <  mark > whether `key` *might* be present in this SST.
    //   false => definitely not present (range/bloom says no).
    //   true  => maybe present (the caller must verify by reading the block or
    //           opening an iterator).
    // Stage S0: this always returns true (no bloom filter parsed yet); a test asserts
    // that the function exists and doesn't crash on an empty table.
    bool may_contain(KeyView key) const;

    // Locate the block index that *would* contain `key`. Binary search over
    // `block_metas_.last_key`. Returns block_metas_.size() if key > all last_keys.
    std::size_t find_block_idx(KeyView key) const;

    // Read the block at `idx` from disk. The returned pointer owns the bytes
    // in RAM; callers should ideally route through a BlockCache (S1/S4).
    // Status return indicates I/O failure.
    // Implementation suggestion (S1): simply `std::ifstream::read` into a
    // `std::string` of size `block_size_bytes`. S4 replaces this with async IO.
    Status read_block(std::size_t idx, std::string& out) const;

    // Open an existing SSTable from disk and parse metadata + bloom.
    // Implementation suggestion (S1):
    //   1. open file (read + binary)
    //   2. read footer (last 24 bytes)
    //   3. read meta blob, deserialize BlockMeta vector
    //   4. (optional) read and decode bloom into `bloom_`
    static Status open(std::uint64_t id, std::filesystem::path path,
                       std::unique_ptr<SSTable>& out);

    // Constant — file magic written at the very end (8 bytes, little endian).
    static constexpr std::uint64_t kMagic = 0x6d696e694c534du; // "MSLinim"

private:
    friend class SSTableBuilder;    // for finish() to set block_data_end_

    std::uint64_t                         id_;
    std::filesystem::path                path_;
    std::vector<BlockMeta>               block_metas_;
    std::vector<unsigned char>           bloom_;
    std::uint64_t                         block_data_end_ = 0; // offset where meta starts
};

// ---------------------------------------------------------------------------
// SSTable iterator — virtual StorageIterator over an open SSTable.
// ---------------------------------------------------------------------------
class SSTableIterator : public StorageIterator {
public:
    explicit SSTableIterator(std::shared_ptr<const SSTable> table);

    bool         is_valid() const override;
    KeyView      key()   const override;
    ValueView    value() const override;
    Status       next()  override;

    void seek_to_key(KeyView target);
    void seek_to_first();

private:
    Status load_current_block();

    std::shared_ptr<const SSTable>  table_;
    std::vector<std::pair<Key, Value>> current_block_entries_;
    std::uint32_t                   current_block_idx_ = 0;
    std::uint32_t                   entry_cursor_      = 0;
    bool                            valid_             = false;
};

} // namespace mini_lsm