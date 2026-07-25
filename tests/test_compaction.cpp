#include "mini_test.hpp"
#include "compaction.h"
#include "lsm_engine.h"

#include <filesystem>

using namespace mini_lsm;

static LsmOptions opts_for(const char* tag) {
    LsmOptions o;
    o.base_dir = std::filesystem::temp_directory_path()
                 / "mini_lsm_s2_tests" / tag;
    std::filesystem::remove_all(o.base_dir);
    std::filesystem::create_directories(o.base_dir);
    return o;
}

TEST(Compaction, FullCompactionControllerType) {
    FullCompactionController ctrl(4);
}

TEST(Compaction, FullCompactionExecutorExists) {
    FullCompactionExecutor exec;
}

TEST(Compaction, ForceFullCompactionNoOpWhenEmpty) {
    auto o = opts_for("ForceFullCompactionNoOpWhenEmpty");
    o.level0_file_num_trigger = 2;
    LsmEngine e(o);
    e.open();
    EXPECT_TRUE(e.force_full_compaction().ok());
}

TEST(Compaction, FlushAndCompactSingleSst) {
    auto o = opts_for("FlushAndCompactSingleSst");
    o.level0_file_num_trigger = 2;
    LsmEngine e(o);
    e.open();

    e.put("k", "v");
    e.force_freeze_memtable();
    std::uint64_t sid = 0;
    e.force_flush_next_imm_memtable(sid);
    EXPECT_GT(sid, 0u);
    EXPECT_EQ(e.snapshot()->l0_sstables.size(), std::size_t{1});

    e.force_full_compaction();
    EXPECT_EQ(e.snapshot()->l0_sstables.size(), std::size_t{1});
}

TEST(Compaction, FlushTwoSstThenCompact) {
    auto o = opts_for("FlushTwoSstThenCompact");
    o.level0_file_num_trigger = 2;
    LsmEngine e(o);
    e.open();

    // SST 1
    e.put("a", "1");
    e.put("b", "2");
    e.force_freeze_memtable();
    std::uint64_t s1 = 0;
    e.force_flush_next_imm_memtable(s1);
    EXPECT_GT(s1, 0u);

    // SST 2
    e.put("c", "3");
    e.put("d", "4");
    e.force_freeze_memtable();
    std::uint64_t s2 = 0;
    e.force_flush_next_imm_memtable(s2);
    EXPECT_GT(s2, 0u);

    EXPECT_EQ(e.snapshot()->l0_sstables.size(), std::size_t{2});

    // Compact -> L0 becomes empty, L1 gets one SST
    EXPECT_TRUE(e.force_full_compaction().ok());

    auto snap = e.snapshot();
    EXPECT_EQ(snap->l0_sstables.size(), std::size_t{0});
    EXPECT_GE(snap->levels.size(), std::size_t{2});
    EXPECT_EQ(snap->levels[1].size(), std::size_t{1});

    // All data readable
    EXPECT_EQ(*e.get("a"), "1");
    EXPECT_EQ(*e.get("b"), "2");
    EXPECT_EQ(*e.get("c"), "3");
    EXPECT_EQ(*e.get("d"), "4");
}
