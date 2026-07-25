#include "mini_test.hpp"
#include "manifest.h"

#include <filesystem>

using namespace mini_lsm;

namespace {
std::filesystem::path tmp_path(const char* tag) {
    auto dir = std::filesystem::temp_directory_path() / "mini_lsm_s2_tests";
    std::filesystem::create_directories(dir);
    auto p = dir / (std::string{"manifest_"} + tag + ".bin");
    std::filesystem::remove(p);
    return p;
}
} // anonymous

TEST(Manifest, OpenCreatesFile) {
    auto p = tmp_path("OpenCreatesFile");
    Manifest m;
    EXPECT_TRUE(m.open(p).ok());
    EXPECT_TRUE(std::filesystem::exists(p));
}

TEST(Manifest, AppendAndRecoverEmpty) {
    auto p = tmp_path("AppendAndRecoverEmpty");
    Manifest m;
    ASSERT_TRUE(m.open(p).ok());

    std::unordered_map<std::uint64_t, std::uint32_t> out;
    EXPECT_TRUE(m.recover(out).ok());
    EXPECT_EQ(out.size(), std::size_t{0});
}

TEST(Manifest, AppendAddAndRecover) {
    auto p = tmp_path("AppendAddAndRecover");
    Manifest m;
    ASSERT_TRUE(m.open(p).ok());

    ManifestRecord r1;
    r1.type = ManifestRecord::kAddSst;
    r1.sst_id = 42;
    r1.level = 0;
    EXPECT_TRUE(m.append(r1).ok());

    ManifestRecord r2;
    r2.type = ManifestRecord::kAddSst;
    r2.sst_id = 99;
    r2.level = 1;
    EXPECT_TRUE(m.append(r2).ok());

    std::unordered_map<std::uint64_t, std::uint32_t> out;
    EXPECT_TRUE(m.recover(out).ok());
    EXPECT_EQ(out.size(), std::size_t{2});
    EXPECT_EQ(out[42], 0u);
    EXPECT_EQ(out[99], 1u);
}

TEST(Manifest, AppendAddAndRemove) {
    auto p = tmp_path("AppendAddAndRemove");
    Manifest m;
    ASSERT_TRUE(m.open(p).ok());

    ManifestRecord add;
    add.type = ManifestRecord::kAddSst;
    add.sst_id = 7;
    add.level = 0;
    EXPECT_TRUE(m.append(add).ok());

    ManifestRecord rem;
    rem.type = ManifestRecord::kRemoveSst;
    rem.sst_id = 7;
    EXPECT_TRUE(m.append(rem).ok());

    std::unordered_map<std::uint64_t, std::uint32_t> out;
    EXPECT_TRUE(m.recover(out).ok());
    EXPECT_EQ(out.size(), std::size_t{0});  // removed
}

TEST(Manifest, MultipleAppendsAndRecover) {
    auto p = tmp_path("MultipleAppendsAndRecover");
    Manifest m;
    ASSERT_TRUE(m.open(p).ok());

    for (std::uint64_t i = 1; i <= 10; ++i) {
        ManifestRecord r;
        r.type = ManifestRecord::kAddSst;
        r.sst_id = i;
        r.level = static_cast<std::uint32_t>(i % 3);
        EXPECT_TRUE(m.append(r).ok());
    }

    std::unordered_map<std::uint64_t, std::uint32_t> out;
    EXPECT_TRUE(m.recover(out).ok());
    EXPECT_EQ(out.size(), std::size_t{10});
    for (std::uint64_t i = 1; i <= 10; ++i) {
        EXPECT_EQ(out[i], i % 3);
    }
}

TEST(Manifest, CorruptedRecordFailsRecovery) {
    auto p = tmp_path("CorruptedRecordFailsRecovery");
    {
        Manifest m;
        ASSERT_TRUE(m.open(p).ok());
        ManifestRecord r;
        r.type = ManifestRecord::kAddSst;
        r.sst_id = 1;
        r.level = 0;
        EXPECT_TRUE(m.append(r).ok());
    }
    // Corrupt the file by appending garbage
    {
        std::ofstream f(p, std::ios::out | std::ios::binary | std::ios::app);
        f.write("garbage", 7);
    }
    Manifest m2;
    EXPECT_TRUE(m2.open(p).ok());
    std::unordered_map<std::uint64_t, std::uint32_t> out;
    auto s = m2.recover(out);
    EXPECT_FALSE(s.ok());  // CRC mismatch or unknown record
}
