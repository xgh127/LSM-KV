// test_compaction.cpp
// -----------------------------------------------------------------------------
// Compaction scaffold tests. S0 only asserts:
//   * NoCompactionController.pick_task returns std::nullopt
//   * NoCompactionExecutor.execute returns kNotSupported
//   * The base class vtable dispatches correctly
// Stage S2 (full) tests will replace these with concrete task generators.
// -----------------------------------------------------------------------------
#include "mini_test.hpp"
#include "compaction.h"
#include "lsm_engine.h"

#include <filesystem>
#include <memory>

using namespace mini_lsm;

TEST(CompactionController, NoOpReturnsNullopt) {
    LsmOptions opts;
    opts.base_dir = std::filesystem::temp_directory_path() / "mini_lsm_compaction_test";
    LsmEngine e(opts);
    e.open();
    NoCompactionController c;
    auto task = c.pick_task(e);
    EXPECT_FALSE(task.has_value());
}

TEST(CompactionController, FlushToL0DefaultTrue) {
    NoCompactionController c;
    EXPECT_TRUE(c.flush_to_l0());
}

TEST(CompactionExecutor, NoOpReturnsNotSupported) {
    NoCompactionExecutor ex;
    LsmOptions opts;
    opts.base_dir = std::filesystem::temp_directory_path() / "mini_lsm_compaction_test2";
    LsmEngine e(opts);
    e.open();
    std::vector<std::uint64_t> new_ids;
    auto s = ex.execute(CompactionTask{}, e, new_ids);
    EXPECT_FALSE(s.ok());
    EXPECT_EQ(static_cast<int>(s.code()), static_cast<int>(Status::Code::kNotSupported));
}

TEST(CompactionTask, DefaultKindIsNone) {
    CompactionTask t;
    EXPECT_EQ(static_cast<int>(t.kind), static_cast<int>(CompactionTask::Kind::kNone));
}