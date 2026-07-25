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

TEST(SSTableBuilder, FinishWritesSstFileOnDisk) {
    SSTableBuilder b(4096, /*bloom=*/true);
    b.add("k1", "v1");
    b.add("k2", "v2");
    std::unique_ptr<SSTable> out;
    auto tmp = std::filesystem::temp_directory_path() / "mini_lsm_s1_test_finish.sst";
    std::filesystem::remove(tmp);
    auto s = b.finish(/*id=*/1, tmp, out);
    EXPECT_TRUE(s.ok());
    EXPECT_TRUE(static_cast<bool>(out));
    EXPECT_EQ(out->num_blocks(), std::size_t{1});
    EXPECT_EQ(out->block_metas().size(), std::size_t{1});
    EXPECT_TRUE(std::filesystem::exists(tmp));
    std::filesystem::remove(tmp);
}

TEST(SSTableBuilder, AscendingInputAccepted) {
    SSTableBuilder b(4096, /*bloom=*/true);
    EXPECT_TRUE(b.add("k1", "v1"));
    EXPECT_TRUE(b.add("k2", "v2"));
    EXPECT_EQ(b.num_entries(), std::size_t{2});
    EXPECT_EQ(b.first_key(), std::string("k1"));
    EXPECT_EQ(b.last_key(), std::string("k2"));
}

TEST(SSTableBuilder, OutOfOrderKeyRejected) {
    SSTableBuilder b(4096, /*bloom=*/true);
    b.add("k2", "v2");
    EXPECT_FALSE(b.add("k1", "v1"));
}

TEST(SSTableBuilder, RoundTripMultipleBlocks) {
    // Use zero-padded keys for lexicographic ordering
    SSTableBuilder b(64, /*bloom=*/false);
    for (int i = 0; i < 30; ++i) {
        char key[8]; std::snprintf(key, sizeof key, "k%04d", i);
        char val[8]; std::snprintf(val, sizeof val, "v%04d", i * 10);
        EXPECT_TRUE(b.add(key, val));
    }
    EXPECT_EQ(b.num_entries(), std::size_t{30});

    std::unique_ptr<SSTable> out;
    auto tmp = std::filesystem::temp_directory_path() / "mini_lsm_s1_multi_block.sst";
    std::filesystem::remove(tmp);
    auto s = b.finish(/*id=*/42, tmp, out);
    EXPECT_TRUE(s.ok());
    EXPECT_GT(out->num_blocks(), std::size_t{1});

    std::size_t total_entries = 0;
    for (auto const& m : out->block_metas()) {
        EXPECT_GT(m.num_entries, 0u);
        EXPECT_FALSE(m.first_key.empty());
        EXPECT_FALSE(m.last_key.empty());
        total_entries += m.num_entries;
    }
    EXPECT_EQ(total_entries, std::size_t{30});

    // Read back via iterator
    SSTableIterator it(std::make_shared<SSTable>(*out));
    int count = 0;
    for (it.seek_to_first(); it.is_valid(); it.next()) {
        char key[8]; std::snprintf(key, sizeof key, "k%04d", count);
        char val[8]; std::snprintf(val, sizeof val, "v%04d", count * 10);
        EXPECT_EQ(it.key(), std::string(key));
        EXPECT_EQ(it.value(), std::string(val));
        ++count;
    }
    EXPECT_EQ(count, 30);

    std::filesystem::remove(tmp);
}