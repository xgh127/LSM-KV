// memtable.cpp
// -----------------------------------------------------------------------------
// S0 stub implementation of MemTable. All bodies are intentionally left as
// the simplest possible "no-op-correct-but-LOSSY" placeholder so the engine
// compiles end-to-end; tests will turn RED until you fill these in.
//
// To implement (Stage S1):
//   * put    -> insert into std::map<Key, Value, std::less<>>
//   * get    -> std::map::find -> return Value or std::nullopt if tombstone
//   * scan   -> return a MemTableIterator holding a shared_ptr<const MemTable>
//               and a std::map::const_iterator positioned at lower bound.
//   * flush_to -> iterate map_->add each entry into builder.
// -----------------------------------------------------------------------------
#include "memtable.h"

#include "sstable_builder.h"
#include <algorithm>

namespace mini_lsm {

// ANONYMOUS iterator --------------------------------------------------------
namespace {
class MemTableIterator final : public StorageIterator {
public:
    explicit MemTableIterator(std::shared_ptr<const MemTable> /*mt*/,
                              KeyView /*lo*/, KeyView /*hi*/)
        : valid_(false) {}

    bool         is_valid() const override { return valid_; }
    KeyView      key()   const override { return {}; }
    ValueView    value() const override { return {}; }
    Status       next()  override { return Status::OK(); }

private:
    bool valid_;
};
} // anonymous namespace

// PUBLIC -----------------------------------------------------------

Status MemTable::put(KeyView /*key*/, ValueView /*value*/) {
    // TODO(S1): map_.emplace(key, value); size_.fetch_add(key.size()+value.size());
    (void)frozen_; // TODO: reject puts if frozen
    return Status::OK();
}

std::optional<Value> MemTable::get(KeyView /*key*/) const {
    // TODO(S1): auto it = map_.find(key); handle tombstone; return value.
    return std::nullopt;
}

std::unique_ptr<StorageIterator> MemTable::scan(KeyView /*lo*/, KeyView /*hi*/) const {
    // TODO(S1): wrap `shared_from_this()` and return iterator positioned at lo.
    // For S0 we return a non-valid iterator so scan tests fail fast but do not
    // segv.
    return std::make_unique<MemTableIterator>(
        std::shared_ptr<const MemTable>{}, KeyView{}, KeyView{});
}

Status MemTable::flush_to(SSTableBuilder& /*builder*/) const {
    // TODO(S1): for (auto& [k, v] : map_) builder.add(k, v);
    return Status::OK();
}

} // namespace mini_lsm