// test_sstable.cpp
// -----------------------------------------------------------------------------
// SSTable + SSTableIterator red-stage tests. Just like test_memtable, these
// will all FAIL until the Stage S1 implementation lands. The whole point of
// S0 is to make sure tests are wired up and the binary does not crash.
// -----------------------------------------------------------------------------
#include "mini_test.hpp"
#include "sstable.h"

#include <filesystem>
#include <memory>

using namespace mini_lsm;

TEST(SSTable, MayContainAlwaysTrueByDefault) {
    auto t = std::make_unique<SSTable>(/*id=*/1, std::filesystem::path{"nonexistent.sst"});
    EXPECT_TRUE(t->may_contain("anything"));
}

TEST(SSTable, FindBlockIdxOnEmptyReturnsZero) {
    auto t = std::make_unique<SSTable>(1, std::filesystem::path{"nonexistent.sst"});
    EXPECT_EQ(t->find_block_idx("k"), std::size_t{0});
}

TEST(SSTable, OpenOnMissingFileReturnsNotSupported) {
    // S0: open() returns kNotSupported; S1 should open and parse metadata.
    std::unique_ptr<SSTable> out;
    auto s = SSTable::open(/*id=*/42,
                           std::filesystem::path{"missing.sst"},
                           out);
    EXPECT_FALSE(s.ok());
}

TEST(SSTableIterator, IteratorStartsInvalid) {
    // Build an SSTable in memory via the constructor only. We do NOT have
    // any real on-disk bytes in S0 — the iterator must therefore be invalid
    // but must not crash. The Stage S1 plan documents that a real open() is
    // the only way to populate metas / bytes.
    auto t = std::make_shared<SSTable>(1, std::filesystem::path{"nonexistent.sst"});
    SSTableIterator it(t);
    EXPECT_FALSE(it.is_valid());
    it.seek_to_first();
    EXPECT_FALSE(it.is_valid());      // still invalid — but no crash.
}

TEST(SSTableIterator, SeekByKeyKeepsInvalid) {
    auto t = std::make_shared<SSTable>(1, std::filesystem::path{"nonexistent.sst"});
    SSTableIterator it(t);
    it.seek_to_key("doesn't-exist");
    EXPECT_FALSE(it.is_valid());
}