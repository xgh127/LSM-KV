#include "mini_test.hpp"
#include "wal.h"

#include <filesystem>

using namespace mini_lsm;

namespace {
std::filesystem::path tmp_path(const char* tag) {
    auto dir = std::filesystem::temp_directory_path() / "mini_lsm_s2_tests";
    std::filesystem::create_directories(dir);
    auto p = dir / (std::string{"wal_"} + tag + ".bin");
    std::filesystem::remove(p);
    return p;
}
} // anonymous

TEST(WAL, OpenCreatesFile) {
    auto p = tmp_path("OpenCreatesFile");
    WAL w;
    EXPECT_TRUE(w.open(p).ok());
    EXPECT_TRUE(std::filesystem::exists(p));
}

TEST(WAL, AppendAndRecoverEmpty) {
    auto p = tmp_path("AppendAndRecoverEmpty");
    WAL w;
    ASSERT_TRUE(w.open(p).ok());
    std::vector<std::pair<Key, Value>> out;
    EXPECT_TRUE(w.recover(out).ok());
    EXPECT_EQ(out.size(), std::size_t{0});
}

TEST(WAL, AppendAndRecoverOne) {
    auto p = tmp_path("AppendAndRecoverOne");
    WAL w;
    ASSERT_TRUE(w.open(p).ok());
    EXPECT_TRUE(w.append("key1", "value1").ok());
    w.close();

    WAL r;
    ASSERT_TRUE(r.open(p).ok());
    std::vector<std::pair<Key, Value>> out;
    EXPECT_TRUE(r.recover(out).ok());
    EXPECT_EQ(out.size(), std::size_t{1});
    EXPECT_EQ(out[0].first, std::string("key1"));
    EXPECT_EQ(out[0].second, std::string("value1"));
}

TEST(WAL, AppendAndRecoverMultiple) {
    auto p = tmp_path("AppendAndRecoverMultiple");
    WAL w;
    ASSERT_TRUE(w.open(p).ok());
    EXPECT_TRUE(w.append("a", "1").ok());
    EXPECT_TRUE(w.append("b", "2").ok());
    EXPECT_TRUE(w.append("c", "3").ok());
    w.close();

    WAL r;
    ASSERT_TRUE(r.open(p).ok());
    std::vector<std::pair<Key, Value>> out;
    EXPECT_TRUE(r.recover(out).ok());
    EXPECT_EQ(out.size(), std::size_t{3});
    EXPECT_EQ(out[1].first, std::string("b"));
    EXPECT_EQ(out[1].second, std::string("2"));
}

TEST(WAL, RecoverFromEmptyFile) {
    auto p = tmp_path("RecoverFromEmptyFile");
    // Create empty file
    std::ofstream f(p, std::ios::out | std::ios::binary);
    f.close();

    WAL w;
    ASSERT_TRUE(w.open(p).ok());
    std::vector<std::pair<Key, Value>> out;
    EXPECT_TRUE(w.recover(out).ok());
    EXPECT_EQ(out.size(), std::size_t{0});
}

TEST(WAL, CorruptedRecordStopsRecovery) {
    auto p = tmp_path("CorruptedRecordStopsRecovery");
    WAL w;
    ASSERT_TRUE(w.open(p).ok());
    EXPECT_TRUE(w.append("good", "data").ok());
    w.close();

    // Append garbage
    {
        std::ofstream f(p, std::ios::out | std::ios::binary | std::ios::app);
        f.write("bad", 3);
    }

    WAL r;
    ASSERT_TRUE(r.open(p).ok());
    std::vector<std::pair<Key, Value>> out;
    EXPECT_TRUE(r.recover(out).ok());  // recovers what it can
    EXPECT_EQ(out.size(), std::size_t{1});  // only good record
    EXPECT_EQ(out[0].first, std::string("good"));
}
