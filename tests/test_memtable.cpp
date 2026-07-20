// test_memtable.cpp
// -----------------------------------------------------------------------------
// MemTable TDD red-stage tests. Until src/memtable.cpp is implemented, every
// non-trivial assertion here should fail — but the binary must not crash.
// Once the bodies are filled in (Stage S1), all tests go green.
// -----------------------------------------------------------------------------
#include "mini_test.hpp"
#include "memtable.h"
#include "sstable_builder.h"

using namespace mini_lsm;

namespace {
// A trivial fixture-style helper: builds a fresh MemTable with a fixed id.
std::shared_ptr<MemTable> make_mt(std::uint64_t id = 1) {
    return std::make_shared<MemTable>(id);
}
} // anonymous

// ---- Basic put/get round-trip ----------------------------------------------
TEST(MemTable, PutGetRoundTrip) {
    auto m = make_mt();
    EXPECT_TRUE(m->put("k1", "v1").ok());
    auto got = m->get("k1");
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got.value(), std::string("v1"));
}

TEST(MemTable, OverwriteLatestValueWins) {
    auto m = make_mt();
    m->put("k", "old");
    m->put("k", "new");
    auto got = m->get("k");
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got.value(), std::string("new"));
}

// ---- Tombstone -------------------------------------------------------------
TEST(MemTable, DeleteReturnsNullopt) {
    auto m = make_mt();
    m->put("k", "v");
    EXPECT_TRUE(m->del("k").ok());
    auto got = m->get("k");
    EXPECT_FALSE(got.has_value());
}

TEST(MemTable, ExplicitEmptyValueIsTombstone) {
    auto m = make_mt();
    m->put("k", "");
    auto got = m->get("k");
    EXPECT_FALSE(got.has_value());
}

// ---- Edge cases ------------------------------------------------------------
TEST(MemTable, GetMissingKey) {
    auto m = make_mt();
    auto got = m->get("not-there");
    EXPECT_FALSE(got.has_value());
}

TEST(MemTable, EmptyValueAndMissingKeyAreBothNulloptButDistinctInternally) {
    auto m = make_mt();
    m->put("absent", "");
    EXPECT_FALSE(m->get("absent").has_value());      // tombstone present
    EXPECT_FALSE(m->get("never").has_value());      // no entry at all
    // We can't directly probe the internal map from here, but a del followed
    // by put("absent", "real") should make it reappear — the engine's filter
    // layer must distinguish the two states per read.
    m->put("absent", "real");
    auto got = m->get("absent");
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(got.value(), std::string("real"));
}

// ---- Capacity & freeze -----------------------------------------------------
TEST(MemTable, ApproximateSizeIncreases) {
    auto m = make_mt();
    auto s0 = m->approximate_size();
    m->put("a", "12345");
    auto s1 = m->approximate_size();
    EXPECT_GT(s1, s0);
}

TEST(MemTable, FreezeReplacesActive) {
    // Implementation hint: after freeze(), the engine creates a new memtable
    // and pushes this one onto immutables. We test only the freeze flag here.
    auto m = make_mt();
    EXPECT_FALSE(m->is_frozen());
    m->freeze();
    EXPECT_TRUE(m->is_frozen());
}

// ---- Scan ordering ---------------------------------------------------------
TEST(MemTable, ScanReturnsSortedIter) {
    auto m = make_mt();
    m->put("c", "1");
    m->put("a", "2");
    m->put("b", "3");
    auto it = m->scan("a", "z");   // [a, z) — all three keys
    ASSERT_TRUE(it != nullptr);

    // Walk the iterator, accumulate keys; expect sorted ascending.
    std::vector<Key> keys;
    while (it->is_valid()) {
        keys.emplace_back(std::string{it->key()});
        it->next();
    }
    EXPECT_EQ(keys.size(), std::size_t{3});
    ASSERT_EQ(keys.size(), std::size_t{3});                  // continue only if 3
    EXPECT_EQ(keys[0], std::string("a"));
    EXPECT_EQ(keys[1], std::string("b"));
    EXPECT_EQ(keys[2], std::string("c"));
}

TEST(MemTable, ScanRespectsLowerBound) {
    auto m = make_mt();
    m->put("a", "1");
    m->put("b", "2");
    m->put("c", "3");
    auto it = m->scan("b", "z");
    ASSERT_TRUE(it != nullptr);
    if (it->is_valid()) {
        EXPECT_EQ(std::string{it->key()}, std::string("b"));
    }
}

TEST(MemTable, ScanRespectsUpperBound) {
    auto m = make_mt();
    m->put("a", "1");
    m->put("b", "2");
    m->put("c", "3");
    auto it = m->scan("a", "b");    // [a, b) — should yield only "a"
    ASSERT_TRUE(it != nullptr);
    std::vector<Key> keys;
    while (it->is_valid()) {
        keys.emplace_back(std::string{it->key()});
        it->next();
    }
    EXPECT_EQ(keys.size(), std::size_t{1});
}

TEST(MemTable, ScanFiltersTombstones) {
    auto m = make_mt();
    m->put("a", "1");
    m->del("b");                    // tombstone at "b"
    m->put("c", "3");
    auto it = m->scan("a", "z");
    ASSERT_TRUE(it != nullptr);
    std::vector<Key> keys;
    while (it->is_valid()) {
        keys.emplace_back(std::string{it->key()});
        it->next();
    }
    EXPECT_EQ(keys.size(), std::size_t{2});   // "a" + "c", "b" filtered
    if (keys.size() == 2) {
        EXPECT_EQ(keys[0], std::string("a"));
        EXPECT_EQ(keys[1], std::string("c"));
    }
}

// ---- flush_to(SSTableBuilder) form check ----------------------------------
TEST(MemTable, FlushToBuilderAcceptsSortedEntries) {
    auto m = make_mt();
    m->put("a", "1");
    m->put("b", "2");
    SSTableBuilder b(/*block_size=*/1024, /*bloom=*/true);
    EXPECT_TRUE(m->flush_to(b).ok());
}