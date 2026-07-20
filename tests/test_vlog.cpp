// test_vlog.cpp
// -----------------------------------------------------------------------------
// VLog red-stage tests. The skeleton's `open()` works (creates dirs + handles),
// `append` is a no-op returning OK, `read_at` returns NotSupported, and `gc`
// is a no-op returning OK with reclaimed=0.
//
// All file IO is confined to a per-test temp directory so the workspace stays
// clean (matches the PLAN.md "不污染当前目录" requirement).
// -----------------------------------------------------------------------------
#include "mini_test.hpp"
#include "vlog.h"

#include <filesystem>

using namespace mini_lsm;

namespace {
std::filesystem::path make_tmp_vlog_path(const char* tag) {
    auto dir = std::filesystem::temp_directory_path() / "mini_lsm_s0_tests";
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

TEST(VLog, AppendReturnsOkEvenInS0) {
    auto p = make_tmp_vlog_path("AppendReturnsOkEvenInS0");
    VLog v;
    ASSERT_TRUE(v.open(p, /*create_if_missing=*/true).ok());
    VLogHandle h{};
    auto s = v.append("key1", "value1", h);
    EXPECT_TRUE(s.ok());
}

TEST(VLog, ReadAtNotSupportedInS0) {
    auto p = make_tmp_vlog_path("ReadAtNotSupportedInS0");
    VLog v;
    ASSERT_TRUE(v.open(p, /*create_if_missing=*/true).ok());
    VLogHandle h{0, 7};
    std::string out;
    auto s = v.read_at(h, out);
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

TEST(VLog, SizeBytesReportsZeroOnFreshOpen) {
    auto p = make_tmp_vlog_path("SizeBytesReportsZeroOnFreshOpen");
    VLog v;
    ASSERT_TRUE(v.open(p, /*create_if_missing=*/true).ok());
    EXPECT_EQ(v.size_bytes(), std::uint64_t{0});
}