#include "memtable.h"

#include "sstable_builder.h"
#include <algorithm>

namespace mini_lsm {

namespace {

using MapIter = std::map<Key, Value, std::less<>>::const_iterator;

class MemTableIterator final : public StorageIterator {
public:
    MemTableIterator(MapIter pos, MapIter end, KeyView hi)
        : end_(end), it_(pos), hi_(hi)
    {
        skip_tombstones_and_bounds();
    }

    bool is_valid() const override { return valid_; }

    KeyView key() const override {
        return it_->first;
    }

    ValueView value() const override {
        return it_->second;
    }

    Status next() override {
        if (!valid_) return Status::OK();
        ++it_;
        skip_tombstones_and_bounds();
        return Status::OK();
    }

private:
    void skip_tombstones_and_bounds() {
        while (it_ != end_) {
            if (!(it_->first < hi_)) {
                valid_ = false;
                return;
            }
            if (it_->second.empty()) {
                ++it_;
                continue;
            }
            break;
        }
        valid_ = it_ != end_;
    }

    MapIter end_;
    MapIter it_;
    KeyView hi_;
    bool valid_ = false;
};

} // anonymous namespace

Status MemTable::put(KeyView key, ValueView value) {
    auto k = Key{key};
    auto v = Value{value};
    auto& existing = map_[k];
    size_.fetch_add(k.size() + v.size() - existing.size());
    existing = std::move(v);
    return Status::OK();
}

std::optional<Value> MemTable::get(KeyView key) const {
    auto it = map_.find(key);
    if (it == map_.end() || it->second.empty()) return std::nullopt;
    return it->second;
}

std::unique_ptr<StorageIterator> MemTable::scan(KeyView lo, KeyView hi) const {
    return std::make_unique<MemTableIterator>(
        map_.lower_bound(lo), map_.end(), hi);
}

Status MemTable::flush_to(SSTableBuilder& builder) const {
    for (auto& [k, v] : map_) {
        builder.add(k, v);
    }
    return Status::OK();
}

} // namespace mini_lsm
