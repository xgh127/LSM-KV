// config.h
// -----------------------------------------------------------------------------
// Engine-wide tunables. Every "magic number" that the reference course PDF
// (Project.LSM-KV-v2.0) hard-codes is lifted here as a struct with sensible
// defaults. The defaults are *not* the course PDF's limitations — they are
// the values a generic textbook LSM-Tree would use (RocksDB / mini-lsm
// flavoured). Override at runtime/instantiation by mutating a LsmOptions
// instance.
//
// The whole skeleton is designed against this struct rather than against
// numbers baked into the code; that is what keeps the design open-ended.
//
// Implementation suggestion: keep ConfigKeys as `static constexpr` so the
// compiler can constant-fold comparison branches for the default config
// while still allowing a different runtime value to be passed in.
// -----------------------------------------------------------------------------
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace mini_lsm {

// Comparator/CRC ------------------------------------------------------------
enum class ChecksumKind { kNone, kCRC32, kXXHash };

// Compaction policy (S3+; stub for S0) ------------------------------------
enum class CompactionPolicy {
    kNoCompaction,
    kSimpleLeveled,
    kTiered,        // RocksDB "universal"
    kLeveled,       // RocksDB "level"
    kHybrid,        // bonus (Dostoevsky-style)
};

struct LsmOptions {
    // --- Directories -------------------------------------------------------
    std::filesystem::path base_dir = "./lsm_data";

    // --- MemTable ----------------------------------------------------------
    // max bytes of the active MemTable before freeze; 2 MiB by default.
    // The course PDF uses a tight 2-page experimental budget — do NOT copy.
    std::size_t memtable_target_size = 2 * 1024 * 1024;
    // how many frozen (immutable) MemTables are allowed to coexist before
    // flushing becomes forced. RocksDB uses `max_write_buffer_number`.
    std::size_t num_memtable_limit   = 4;

    // --- SST / Block -------------------------------------------------------
    std::size_t block_size_bytes     = 4 * 1024;   // 4 KiB block (filesystem page).
    std::size_t target_sst_size      = 4 * 1024 * 1024;  // per-SST target.
    bool        prefix_key_compression = true;
    bool        bloom_filter_enabled    = true;
    // Bits per key for bloom filter; 10 -> ~1% FPR per mini-lsm tutorial.
    double      bloom_bits_per_key      = 10.0;

    // --- Compaction --------------------------------------------------------
    CompactionPolicy compaction_policy = CompactionPolicy::kSimpleLeveled;
    std::size_t max_levels             = 7;
    std::size_t level0_file_num_trigger = 4;
    std::size_t level_size_multiplier   = 10;

    // --- Write-Ahead Log ---------------------------------------------------
    bool       enable_wal         = false;
    // fsync policy: -1 = disabled, 0 = fsync on every put, N = fsync every N ms.
    int        wal_flush_interval_ms = 50;
    ChecksumKind record_checksum    = ChecksumKind::kCRC32;

    // --- Block Cache (S1 placeholder for S4 enhancement) -----------------
    std::size_t block_cache_capacity = 8 * 1024 * 1024;

    // --- Misc ---------------------------------------------------------------
    bool       direct_io           = false;   // O_DIRECT for SST read
    bool       use_async_io        = false;   // io_uring / IOCP (S4)
    std::uint32_t seed_for_hashing = 0xC0FFEEu;
};

} // namespace mini_lsm