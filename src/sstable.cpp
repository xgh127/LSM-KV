// sstable.cpp
// -----------------------------------------------------------------------------
// Stub implementation of SSTable + SSTableIterator.
//
// Format details and full I/O are Stage S1 work. S0 only needs:
//   * constructor + accessors compile
//   * may_contain() / find_block_idx() exist and don't crash on empty metas
//   * open() returns kNotSupported so tests fail gracefully
// -----------------------------------------------------------------------------
#include "sstable.h"

#include <algorithm>

namespace mini_lsm {

// SSTable --------------------------------------------------------------------
SSTable::SSTable(std::uint64_t id, std::filesystem::path path)
    : id_(id), path_(std::move(path)) {}

bool SSTable::may_contain(KeyView /*key*/) const {
    // TODO(S1): if bloom is present, probe it; otherwise range-check first/last.
    // For S0, we are conservative: return true (i.e. "might contain") — never
    // crash the iterator path, even though the engine has no real SST yet.
    return true;
}

std::size_t SSTable::find_block_idx(KeyView /*key*/) const {
    // TODO(S1): binary search `block_metas_.last_key`.
    // S0 returns 0 unconditionally (handle gracefully when metas is empty).
    if (block_metas_.empty()) return 0;
    return 0;
}

Status SSTable::read_block(std::size_t /*idx*/, std::string& /*out*/) const {
    // TODO(S1): ifstream seekg(block_metas_[idx].offset); read block_size_ bytes.
    return Status::NotSupported("SSTable::read_block is a S1 TODO");
}

Status SSTable::open(std::uint64_t /*id*/, std::filesystem::path /*path*/,
                     std::unique_ptr<SSTable>& /*out*/) {
    // TODO(S1): read footer, parse meta, (optional) bloom.
    return Status::NotSupported("SSTable::open is a S1 TODO");
}

// SSTableIterator ------------------------------------------------------------
SSTableIterator::SSTableIterator(std::shared_ptr<const SSTable> table)
    : table_(std::move(table)) {}

bool       SSTableIterator::is_valid() const      { return valid_; }
KeyView    SSTableIterator::key()   const        { return {}; }
ValueView  SSTableIterator::value() const        { return {}; }
Status     SSTableIterator::next()                { return Status::OK(); }

void SSTableIterator::seek_to_first() {
    // TODO(S1): read_block(0), parse first entry.
    valid_ = false;
}

void SSTableIterator::seek_to_key(KeyView /*target*/) {
    // TODO(S1): find_block_idx(target); read_block; within-block scan.
    valid_ = false;
}

} // namespace mini_lsm