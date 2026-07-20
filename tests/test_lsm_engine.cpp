// test_lsm_engine.cpp
// -----------------------------------------------------------------------------
// LsmEngine end-to-end red-stage tests. These mirror the layout of
// `LSM-c++/correctness.cc` (regular_test, gc_test) but driven through the
// new LsmEngine public API.
//
// All file I/O is confined to a per-test temporary directory to satisfy the
// "不污染当前目录 / use temp files" requirement.
// -----------------------------------------------------------------------------
#include "mini_test.hpp"
#include "lsm_engine.h"

#include <filesystem>
#include <vector>

using namespace mini_lsm;

namespace {
LsmOptions opts_with_temp_dir(const char* tag) {
    LsmOptions o;
    o.base_dir = std::filesystem::temp_directory_path()
                 / "mini_lsm_engine_tests" / tag;
    std::filesystem::remove_all(o.base_dir);
    std::filesystem::create_directories(o.base_dir);
    return o;
}
} // anonymous

// ---- Lifecycle -------------------------------------------------------------
TEST(LsmEngine, OpenCreatesBaseDir) {
    auto o = opts_with_temp_dir("OpenCreatesBaseDir");
    LsmEngine e(o);
    EXPECT_TRUE(e.open().ok());
    EXPECT_TRUE(std::filesystem::exists(o.base_dir));
}

TEST(LsmEngine, CloseIsIdempotent) {
    auto o = opts_with_temp_dir("CloseIsIdempotent");
    LsmEngine e(o);
    e.open();
    EXPECT_TRUE(e.close().ok());
    EXPECT_TRUE(e.close().ok());
}

TEST(LsmEngine, ResetClearsInMemoryState) {
    auto o = opts_with_temp_dir("ResetClearsInMemoryState");
    LsmEngine e(o);
    e.open();
    e.put("k", "v");
    auto snap = e.snapshot();
    EXPECT_EQ(snap->l0_sstables.size(), std::size_t{0});
    EXPECT_TRUE(e.reset().ok());
    auto snap2 = e.snapshot();
    EXPECT_EQ(snap2->l0_sstables.size(), std::size_t{0});
    EXPECT_EQ(snap2->immutable_memtables.size(), std::size_t{0});
}

// ---- Basic I/O -------------------------------------------------------------
TEST(LsmEngine, PutGetRoundTrip) {
    auto o = opts_with_temp_dir("PutGetRoundTrip");
    LsmEngine e(o);
    e.open();
    e.put("k1", "v1");
    auto got = e.get("k1");
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got.value(), std::string("v1"));
}

TEST(LsmEngine, GetMissingKeyReturnsNullopt) {
    auto o = opts_with_temp_dir("GetMissingKeyReturnsNullopt");
    LsmEngine e(o);
    e.open();
    auto got = e.get("not-there");
    EXPECT_FALSE(got.has_value());
}

TEST(LsmEngine, DeleteIsTombstone) {
    auto o = opts_with_temp_dir("DeleteIsTombstone");
    LsmEngine e(o);
    e.open();
    e.put("k", "v");
    e.del("k");
    EXPECT_FALSE(e.get("k").has_value());
}

TEST(LsmEngine, OverwriteValueWins) {
    auto o = opts_with_temp_dir("OverwriteValueWins");
    LsmEngine e(o);
    e.open();
    e.put("k", "old");
    e.put("k", "new");
    auto got = e.get("k");
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got.value(), std::string("new"));
}

// ---- Batch operations / repeated put -------------------------------------
TEST(LsmEngine, HundredPutsAllGettable) {
    auto o = opts_with_temp_dir("HundredPutsAllGettable");
    LsmEngine e(o);
    e.open();
    for (int i = 0; i < 100; ++i) {
        e.put(std::to_string(i), std::to_string(i * 10));
    }
    for (int i = 0; i < 100; ++i) {
        auto got = e.get(std::to_string(i));
        ASSERT_TRUE(got.has_value());
        EXPECT_EQ(got.value(), std::to_string(i * 10));
    }
}

// ---- Scan ------------------------------------------------------------------
TEST(LsmEngine, ScanReturnsSortedInRange) {
    auto o = opts_with_temp_dir("ScanReturnsSortedInRange");
    LsmEngine e(o);
    e.open();
    e.put("a", "1");
    e.put("b", "2");
    e.put("c", "3");
    std::vector<std::pair<Key, Value>> out;
    EXPECT_TRUE(e.scan("a", "z", out).ok());
    EXPECT_EQ(out.size(), std::size_t{3});
    if (out.size() == 3) {
        EXPECT_EQ(out[0].first,  std::string("a"));
        EXPECT_EQ(out[0].second, std::string("1"));
        EXPECT_EQ(out[1].first,  std::string("b"));
        EXPECT_EQ(out[1].second, std::string("2"));
        EXPECT_EQ(out[2].first,  std::string("c"));
        EXPECT_EQ(out[2].second, std::string("3"));
    }
}

TEST(LsmEngine, ScanOmitsTombstones) {
    auto o = opts_with_temp_dir("ScanOmitsTombstones");
    LsmEngine e(o);
    e.open();
    e.put("a", "1");
    e.put("b", "2");
    e.del("b");
    e.put("c", "3");
    std::vector<std::pair<Key, Value>> out;
    EXPECT_TRUE(e.scan("a", "z", out).ok());
    EXPECT_EQ(out.size(), std::size_t{2});
    if (out.size() == 2) {
        EXPECT_EQ(out[0].first, std::string("a"));
        EXPECT_EQ(out[1].first, std::string("c"));
    }
}

// ---- Snapshot / freeze / flush ladders -----------------------------------
TEST(LsmEngine, ForceFreezePushesToImmutables) {
    auto o = opts_with_temp_dir("ForceFreezePushesToImmutables");
    LsmEngine e(o);
    e.open();
    e.put("k", "v");
    EXPECT_EQ(e.snapshot()->immutable_memtables.size(), std::size_t{0});
    EXPECT_TRUE(e.force_freeze_memtable().ok());
    EXPECT_EQ(e.snapshot()->immutable_memtables.size(), std::size_t{1});
}

TEST(LsmEngine, ForceFlushPopsImmutable) {
    auto o = opts_with_temp_dir("ForceFlushPopsImmutable");
    LsmEngine e(o);
    e.open();
    e.put("k", "v");
    e.force_freeze_memtable();
    std::uint64_t new_sst_id = 0;
    EXPECT_TRUE(e.force_flush_next_imm_memtable(new_sst_id).ok());
    EXPECT_EQ(e.snapshot()->immutable_memtables.size(), std::size_t{0});
}

// ---- Background thread lifecycle ------------------------------------------
TEST(LsmEngine, StartStopFlushThreadIdempotent) {
    auto o = opts_with_temp_dir("StartStopFlushThreadIdempotent");
    LsmEngine e(o);
    e.open();
    e.start_flush_thread();
    e.start_flush_thread();                      // double start is no-op
    e.stop_flush_thread();
    e.stop_flush_thread();                       // double stop is no-op
    EXPECT_TRUE(true);                           // no crash, no hang
}