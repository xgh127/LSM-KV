#include "compaction.h"
#include "lsm_engine.h"
#include "sstable_builder.h"

#include <algorithm>
#include <queue>
#include <vector>

namespace mini_lsm {

Status NoCompactionExecutor::execute(const CompactionTask&, LsmEngine&,
                                     std::vector<std::uint64_t>&) {
    return Status::NotSupported("NoCompactionExecutor: no work in S0 skeleton");
}

// FullCompactionController ---------------------------------------------------

FullCompactionController::FullCompactionController(std::size_t l0_trigger)
    : l0_trigger_(l0_trigger) {}

std::optional<CompactionTask> FullCompactionController::pick_task(
    const LsmEngine& engine) const {
    // Only trigger when L0 count exceeds threshold
    auto snap = engine.snapshot();
    if (snap->l0_sstables.size() < l0_trigger_) return std::nullopt;

    CompactionTask task;
    task.kind = CompactionTask::Kind::kForceFull;
    task.upper_level = 0;
    task.upper_ssts = snap->l0_sstables;
    task.lower_level = 1;
    task.is_bottom_level = false;
    return task;
}

// MergeIterator ---------------------------------------------------------------

class MergeIterator : public StorageIterator {
public:
    using IterPtr = std::unique_ptr<StorageIterator>;

    explicit MergeIterator(std::vector<IterPtr> children)
        : children_(std::move(children)) {
        // Initialize heap
        for (std::size_t i = 0; i < children_.size(); ++i) {
            if (children_[i] && children_[i]->is_valid()) {
                heap_.push(HeapEntry{children_[i]->key(), i});
            }
        }
        if (!heap_.empty()) pop_min();
    }

    bool is_valid() const override { return valid_; }

    KeyView key() const override {
        return current_key_;
    }

    ValueView value() const override {
        return current_value_;
    }

    Status next() override {
        if (!valid_) return Status::OK();

        // Advance the child that contributed current_min
        auto s = children_[current_child_idx_]->next();
        if (!s.ok()) {
            valid_ = false;
            return s;
        }

        // Push new entry from that child if still valid
        if (children_[current_child_idx_]->is_valid()) {
            heap_.push(HeapEntry{children_[current_child_idx_]->key(),
                                 current_child_idx_});
        }

        pop_min();
        return Status::OK();
    }

private:
    struct HeapEntry {
        KeyView      key;
        std::size_t  child_idx;

        bool operator<(HeapEntry const& o) const {
            // We want min-heap, so reverse comparison
            return key > o.key;
        }
    };

    void pop_min() {
        while (!heap_.empty()) {
            auto top = heap_.top();
            heap_.pop();

            if (!children_[top.child_idx]->is_valid()) continue;

            // Skip duplicates: if same key, keep only the first (highest priority)
            if (top.key == current_key_) {
                // Advance the duplicate child
                children_[top.child_idx]->next();
                if (children_[top.child_idx]->is_valid()) {
                    heap_.push(HeapEntry{children_[top.child_idx]->key(),
                                         top.child_idx});
                }
                continue;
            }

            current_child_idx_ = top.child_idx;
            current_key_ = Key{top.key};
            current_value_ = Value{children_[top.child_idx]->value()};
            valid_ = true;
            return;
        }
        valid_ = false;
    }

    std::vector<IterPtr>          children_;
    std::priority_queue<HeapEntry> heap_;
    Key                            current_key_;
    Value                          current_value_;
    std::size_t                    current_child_idx_ = 0;
    bool                           valid_ = false;
};

// FullCompactionExecutor -----------------------------------------------------

Status FullCompactionExecutor::execute(const CompactionTask& task,
                                       LsmEngine& engine,
                                       std::vector<std::uint64_t>& new_sst_ids) {
    if (task.upper_ssts.empty()) {
        return Status::OK();
    }

    // Open iterators for all upper-level SSTables
    auto snap = engine.snapshot();
    std::vector<std::unique_ptr<StorageIterator>> iters;
    for (auto sst_id : task.upper_ssts) {
        auto it = snap->sstables.find(sst_id);
        if (it == snap->sstables.end()) continue;
        auto iter = std::make_unique<SSTableIterator>(
            std::const_pointer_cast<SSTable>(it->second));
        iter->seek_to_first();
        if (iter->is_valid()) {
            iters.push_back(std::move(iter));
        }
    }

    if (iters.empty()) return Status::OK();

    // Merge all iterators
    MergeIterator merger(std::move(iters));

    // Build new SSTable
    SSTableBuilder builder(engine.options().block_size_bytes,
                           engine.options().bloom_filter_enabled);
    while (merger.is_valid()) {
        builder.add(merger.key(), merger.value());
        merger.next();
    }

    auto new_id = engine.next_sst_id();
    auto sst_path = engine.options().base_dir /
                    (std::to_string(new_id) + ".sst");
    std::unique_ptr<SSTable> tbl;
    auto s = builder.finish(new_id, sst_path, tbl);
    if (!s.ok()) return s;

    new_sst_ids.push_back(new_id);
    return Status::OK();
}

} // namespace mini_lsm
