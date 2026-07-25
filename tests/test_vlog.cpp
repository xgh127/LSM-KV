// test_vlog.cpp
// -----------------------------------------------------------------------------
// VLog tests. S0: open/append/read_at/gc API checked. S1: real append+read_at
// round-trip verified.
// -----------------------------------------------------------------------------
#include "mini_test.hpp"
#include "vlog.h"

#include <filesystem>

using namespace mini_lsm;

namespace {
std::filesystem::path make_tmp_vlog_path(const char* tag) {
    auto dir = std::filesystem::temp_directory_path() / "mini_lsm_tests";
    std::filesystem::create_directories(dir);
    auto p = dir / (std::string{"vlog_"} + tag + ".bin");
    std::filesystem::remove(p);
    return p;
}
} // anonymous

TEST(VLog, OpenCreatesFile) {
    auto p = make_tmp_vlog_path("OpenCreatesFile");
    VLog v;
    auto s = v.open(p, /*create_if_missing=*/true);
    EXPECT_TRUE(s.ok());
    EXPECT_TRUE(std::filesystem::exists(p));
}

TEST(VLog, OpenWithoutCreateFailsOnMissingFile) {
    auto p = make_tmp_vlog_path("OpenWithoutCreateFailsOnMissingFile");
    std::filesystem::remove(p);
    VLog v;
    auto s = v.open(p, /*create_if_missing=*/false);
    EXPECT_FALSE(s.ok());
}

TEST(VLog, AppendAndReadAtRoundTrip) {
    auto p = make_tmp_vlog_path("AppendAndReadAtRoundTrip");
    VLog v;
    ASSERT_TRUE(v.open(p, /*create_if_missing=*/true).ok());
    VLogHandle h;
    auto s = v.append("mykey", "myvalue", h);
    EXPECT_TRUE(s.ok());
    EXPECT_GT(h.length, 0u);
    EXPECT_EQ(v.size_bytes(), h.length);

    std::string out;
    auto s2 = v.read_at(h, out);
    EXPECT_TRUE(s2.ok());
    EXPECT_EQ(out, std::string("myvalue"));
}

TEST(VLog, AppendMultipleAndReadEach) {
    auto p = make_tmp_vlog_path("AppendMultipleAndReadEach");
    VLog v;
    ASSERT_TRUE(v.open(p, true).ok());
    VLogHandle h1, h2, h3;
    EXPECT_TRUE(v.append("ka", "va", h1).ok());
    EXPECT_TRUE(v.append("kb", "vb", h2).ok());
    EXPECT_TRUE(v.append("kc", "vc", h3).ok());
    EXPECT_LT(h1.offset, h2.offset);
    EXPECT_LT(h2.offset, h3.offset);

    std::string out;
    EXPECT_TRUE(v.read_at(h2, out).ok());
    EXPECT_EQ(out, "vb");
    EXPECT_TRUE(v.read_at(h3, out).ok());
    EXPECT_EQ(out, "vc");
    EXPECT_TRUE(v.read_at(h1, out).ok());
    EXPECT_EQ(out, "va");
}

TEST(VLog, ReadAtInvalidHandleFails) {
    auto p = make_tmp_vlog_path("ReadAtInvalidHandleFails");
    VLog v;
    ASSERT_TRUE(v.open(p, true).ok());
    VLogHandle bad{0, 1};  // too small for valid record
    std::string out;
    auto s = v.read_at(bad, out);
    EXPECT_FALSE(s.ok());
}

TEST(VLog, GcIsCallableAndNoOp) {
    auto p = make_tmp_vlog_path("GcIsCallableAndNoOp");
    VLog v;
    ASSERT_TRUE(v.open(p, /*create_if_missing=*/true).ok());
    std::uint64_t reclaimed = 0xDEADBEEFul;
    auto s = v.gc(/*chunk_size=*/1024, reclaimed);
    EXPECT_TRUE(s.ok());
    EXPECT_EQ(reclaimed, std::uint64_t{0});
}

TEST(VLog, SizeBytesReportsCorrectly) {
    auto p = make_tmp_vlog_path("SizeBytesReportsCorrectly");
    VLog v;
    ASSERT_TRUE(v.open(p, true).ok());
    EXPECT_EQ(v.size_bytes(), 0u);
    VLogHandle h;
    EXPECT_TRUE(v.append("k", "v", h).ok());
    EXPECT_EQ(v.size_bytes(), h.length);
    EXPECT_TRUE(v.append("k2", "v2", h).ok());
    EXPECT_EQ(v.size_bytes(), h.offset + h.length);
}