// test_sstable_builder.cpp
// -----------------------------------------------------------------------------
// S0 red-stage tests for SSTableBuilder. The skeleton's `add` returns false
// and `finish` returns kNotSupported; tests check those contracts and pick
// up the implementation in Stage S1.
// -----------------------------------------------------------------------------
#include "mini_test.hpp"
#include "sstable_builder.h"

#include <filesystem>
#include <memory>

using namespace mini_lsm;

TEST(SSTableBuilder, EmptyBuilderIsEmpty) {
    SSTableBuilder b(/*block_size=*/4096, /*bloom=*/true);
    EXPECT_TRUE(b.is_empty());
    EXPECT_EQ(b.num_entries(), std::size_t{0});
    EXPECT_EQ(b.estimated_size_bytes(), std::size_t{0});
}

TEST(SSTableBuilder, AddAcceptsAscending) {
    SSTableBuilder b(4096, /*bloom=*/true);
    EXPECT_TRUE(b.add("k1", "v1"));
    EXPECT_TRUE(b.add("k2", "v2"));
}

TEST(SSTableBuilder, FinishReturnsNotSupportedInS0) {
    SSTableBuilder b(4096, /*bloom=*/true);
    std::unique_ptr<SSTable> out;
    auto tmp = std::filesystem::temp_directory_path() / "mini_lsm_s0_test.sst";
    auto s = b.finish(/*id=*/1, tmp, out);
    EXPECT_FALSE(s.ok());
}

TEST(SSTableBuilder, AscendingInputIsAcceptedOnceImplemented) {
    // This test stays RED in S0 (because add returns false). It will go GREEN
    // in S1 once the ascending-order path is implemented. The TEST documents
    // the intended S1 contract.
    SSTableBuilder b(4096, /*bloom=*/true);
    EXPECT_TRUE(b.add("k1", "v1"));   // RED in S0
    EXPECT_TRUE(b.add("k2", "v2"));   // RED in S0
    EXPECT_EQ(b.num_entries(), std::size_t{2});
}

TEST(SSTableBuilder, OutOfOrderKeyRejected) {
    // S1 contract: out-of-order insertion is forbidden.
    SSTableBuilder b(4096, /*bloom=*/true);
    b.add("k2", "v2");
    EXPECT_FALSE(b.add("k1", "v1"));   // never allowed, even in S0
}