// compaction.cpp
// -----------------------------------------------------------------------------
// S0 compaction stubs.
// -----------------------------------------------------------------------------
#include "compaction.h"

namespace mini_lsm {

Status NoCompactionExecutor::execute(const CompactionTask&, LsmEngine&,
                                     std::vector<std::uint64_t>&) {
    return Status::NotSupported("NoCompactionExecutor: no work in S0 skeleton");
}

} // namespace mini_lsm