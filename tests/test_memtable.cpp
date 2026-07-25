// test_memtable.cpp
// -----------------------------------------------------------------------------
// MemTable TDD tests — grouped by function.
// Check off each group as you implement the corresponding method:
//
//   [ ] Put  ─── PutBasic, PutOverwrite, PutThenGetSize
//   [ ] Get  ─── GetExisting, GetMissing, GetAfterDelete, GetTombstoneVsMissing
//   [ ] Scan ─── ScanOrdered, ScanLowerBound, ScanUpperBound, ScanSkipsTombstone
//   [ ] FlushTo ── FlushToBuilder
//   [ ] Misc  ─── Freeze
// -----------------------------------------------------------------------------
#include "mini_test.hpp"
#include "memtable.h"
#include "sstable_builder.h"

using namespace mini_lsm;

namespace {
std::shared_ptr<MemTable> make_mt(std::uint64_t id = 1) {
    return std::make_shared<MemTable>(id);
}
} // anonymous

// ============================================================================
// Put
// ============================================================================

TEST(MemTable, PutBasic) {
    auto m = make_mt();
    EXPECT_TRUE(m->put("k", "v").ok());
}

TEST(MemTable, PutThenGet) {
    auto m = make_mt();
    EXPECT_TRUE(m->put("k", "v").ok());
    auto got = m->get("k");
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(*got, "v");
}

TEST(MemTable, PutOverwrite) {
    auto m = make_mt();
    m->put("k", "old");
    m->put("k", "new");
    auto got = m->get("k");
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(*got, "new");
}

TEST(MemTable, PutMultipleKeys) {
    auto m = make_mt();
    EXPECT_TRUE(m->put("a", "1").ok());
    EXPECT_TRUE(m->put("b", "2").ok());
    EXPECT_TRUE(m->put("c", "3").ok());
    auto ga = m->get("a"); ASSERT_TRUE(ga.has_value()); EXPECT_EQ(*ga, "1");
    auto gb = m->get("b"); ASSERT_TRUE(gb.has_value()); EXPECT_EQ(*gb, "2");
    auto gc = m->get("c"); ASSERT_TRUE(gc.has_value()); EXPECT_EQ(*gc, "3");
}

TEST(MemTable, PutSizeIncreases) {
    auto m = make_mt();
    auto before = m->approximate_size();
    m->put("k", "hello");
    EXPECT_GT(m->approximate_size(), before);
}

TEST(MemTable, PutSizeTracksOverwrite) {
    auto m = make_mt();
    m->put("k", "aaaaa");
    auto after_short = m->approximate_size();
    m->put("k", "bbbbbbbbbb");
    auto after_long = m->approximate_size();
    EXPECT_GT(after_long, after_short);
}

TEST(MemTable, PutSizeTracksDelete) {
    auto m = make_mt();
    m->put("k", "aaaaa");
    auto with_data = m->approximate_size();
    m->del("k");
    auto after_del = m->approximate_size();
    EXPECT_LT(after_del, with_data);
}

// ============================================================================
// Get
// ============================================================================

TEST(MemTable, GetExisting) {
    auto m = make_mt();
    m->put("k", "v");
    auto got = m->get("k");
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(*got, "v");
}

TEST(MemTable, GetMissing) {
    auto m = make_mt();
    auto got = m->get("not-there");
    EXPECT_FALSE(got.has_value());
}

TEST(MemTable, GetAfterDelete) {
    auto m = make_mt();
    m->put("k", "v");
    m->del("k");
    EXPECT_FALSE(m->get("k").has_value());
}

TEST(MemTable, GetTombstoneVsMissing) {
    auto m = make_mt();
    m->put("absent", "");           // tombstone: empty value
    EXPECT_FALSE(m->get("absent").has_value());   // both return nullopt
    EXPECT_FALSE(m->get("never").has_value());
    // but tombstone can be resurrected by a new put
    m->put("absent", "real");
    auto got = m->get("absent");
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(*got, "real");
}

TEST(MemTable, GetExplicitTombstone) {
    auto m = make_mt();
    m->put("k", "");
    EXPECT_FALSE(m->get("k").has_value());
}

// ============================================================================
// Scan
// ============================================================================

TEST(MemTable, ScanOrdered) {
    auto m = make_mt();
    m->put("c", "1");
    m->put("a", "2");
    m->put("b", "3");
    auto it = m->scan("a", "z");
    EXPECT_TRUE(it != nullptr);

    std::vector<Key> keys;
    while (it->is_valid()) {
        keys.push_back(Key{it->key()});
        it->next();
    }
    EXPECT_EQ(keys.size(), 3u);
    if (keys.size() == 3) {
        EXPECT_EQ(keys[0], "a");
        EXPECT_EQ(keys[1], "b");
        EXPECT_EQ(keys[2], "c");
    }
}

TEST(MemTable, ScanLowerBound) {
    auto m = make_mt();
    m->put("a", "1");
    m->put("b", "2");
    m->put("c", "3");
    auto it = m->scan("b", "z");
    EXPECT_TRUE(it != nullptr);
    ASSERT_TRUE(it->is_valid());
    EXPECT_EQ(Key{it->key()}, "b");
}

TEST(MemTable, ScanUpperBound) {
    auto m = make_mt();
    m->put("a", "1");
    m->put("b", "2");
    m->put("c", "3");
    auto it = m->scan("a", "b");
    EXPECT_TRUE(it != nullptr);
    std::vector<Key> keys;
    while (it->is_valid()) {
        keys.push_back(Key{it->key()});
        it->next();
    }
    EXPECT_EQ(keys.size(), 1u);
    if (keys.size() == 1) {
        EXPECT_EQ(keys[0], "a");
    }
}

TEST(MemTable, ScanSkipsTombstone) {
    auto m = make_mt();
    m->put("a", "1");
    m->del("b");
    m->put("c", "3");
    auto it = m->scan("a", "z");
    EXPECT_TRUE(it != nullptr);
    std::vector<Key> keys;
    while (it->is_valid()) {
        keys.push_back(Key{it->key()});
        it->next();
    }
    EXPECT_EQ(keys.size(), 2u);
    if (keys.size() == 2) {
        EXPECT_EQ(keys[0], "a");
        EXPECT_EQ(keys[1], "c");
    }
}

TEST(MemTable, ScanEmptyRange) {
    auto m = make_mt();
    m->put("a", "1");
    auto it = m->scan("z", "zz");
    EXPECT_TRUE(it != nullptr);
    EXPECT_FALSE(it->is_valid());
}

TEST(MemTable, ScanEmptyMemtable) {
    auto m = make_mt();
    auto it = m->scan("a", "z");
    EXPECT_TRUE(it != nullptr);
    EXPECT_FALSE(it->is_valid());
}

TEST(MemTable, ScanAtExactBound) {
    auto m = make_mt();
    m->put("a", "1");
    m->put("b", "2");
    auto it = m->scan("b", "b");
    EXPECT_TRUE(it != nullptr);
    EXPECT_FALSE(it->is_valid());
}

// ============================================================================
// FlushTo
// ============================================================================

TEST(MemTable, FlushToBuilder) {
    auto m = make_mt();
    m->put("a", "1");
    m->put("b", "2");
    SSTableBuilder b(1024, true);
    EXPECT_TRUE(m->flush_to(b).ok());
}

// ============================================================================
// Freeze
// ============================================================================

TEST(MemTable, Freeze) {
    auto m = make_mt();
    EXPECT_FALSE(m->is_frozen());
    m->freeze();
    EXPECT_TRUE(m->is_frozen());
}
