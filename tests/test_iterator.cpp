// test_iterator.cpp
// -----------------------------------------------------------------------------
// StorageIterator abstract contract tests. In S0 we only have one concrete
// iterator (SSTableIterator) and it's a stub; the tests verify the abstract
// interface is *implemented* (vtable present) without depending on any
// real disk content.
// -----------------------------------------------------------------------------
#include "mini_test.hpp"
#include "iterator.h"
#include "sstable.h"

#include <filesystem>
#include <memory>

using namespace mini_lsm;

namespace {
// A tiny "fake iterator" used to assert the StorageIterator virtual API
// works through polymorphic pointers. This is exactly the gtest Mock-style
// pattern but written inline because S0 has no gmock.
class FakeIter final : public StorageIterator {
public:
    explicit FakeIter(int count) : remaining_(count) {}
    bool       is_valid() const override        { return remaining_ > 0; }
    KeyView    key()   const override          { return "k"; }
    ValueView  value() const override          { return "v"; }
    Status     next()  override                { if (remaining_ > 0) --remaining_; return Status::OK(); }
    std::size_t num_active_iterators() const override { return 1; }
private:
    int remaining_;
};
} // anonymous

TEST(StorageIterator, VirtualDispatchWorks) {
    std::unique_ptr<StorageIterator> it = std::make_unique<FakeIter>(3);
    ASSERT_TRUE(it != nullptr);
    EXPECT_TRUE(it->is_valid());
    EXPECT_TRUE(it->next().ok());
    EXPECT_TRUE(it->is_valid());
    it->next();
    it->next();
    EXPECT_FALSE(it->is_valid());
}

TEST(StorageIterator, NumActiveIteratorsDefault) {
    auto it = std::make_unique<FakeIter>(1);
    EXPECT_EQ(it->num_active_iterators(), std::size_t{1});
}

TEST(StorageIterator, SSTableIteratorIsAStorageIterator) {
    // Compile-time check: SSTableIterator must derive from StorageIterator.
    auto t = std::make_shared<SSTable>(1, std::filesystem::path{"unused.sst"});
    std::unique_ptr<StorageIterator> it = std::make_unique<SSTableIterator>(t);
    EXPECT_FALSE(it->is_valid());
}