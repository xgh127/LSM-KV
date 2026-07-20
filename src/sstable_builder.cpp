// sstable_builder.cpp
// -----------------------------------------------------------------------------
// S0 stub of SSTableBuilder. `add` and `finish` deliberately do no real I/O;
// tests for the S0 skeleton only assert that the API exists and that
// "trivially-correct" calls succeed. The Stage S1 plan documents the expected
// encoding (header on top, BlockMeta serialization, optional bloom).
// -----------------------------------------------------------------------------
#include "sstable_builder.h"

namespace mini_lsm {

SSTableBuilder::SSTableBuilder(std::size_t block_size, bool bloom_enabled)
    : block_size_(block_size), bloom_enabled_(bloom_enabled) {}

bool SSTableBuilder::add(KeyView /*key*/, ValueView /*value*/) {
    // TODO(S1): enforce ascending check, encode into a per-block buffer,
    // split on block_size_, record BlockMeta, update first/last key.
    // S0 returns false so tests see the red state immediately.
    return false;
}

Status SSTableBuilder::finish(std::uint64_t /*id*/, std::filesystem::path /*path*/,
                              std::unique_ptr<SSTable>& /*out*/) {
    // TODO(S1): flush current block, write meta blob, write optional bloom,
    // write footer, fsync, close, re-open via SSTable::open.
    // S0 returns kNotSupported so tests fail gracefully.
    return Status::NotSupported("SSTableBuilder::finish is a S1 TODO");
}

} // namespace mini_lsm