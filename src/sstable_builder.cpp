#include "sstable_builder.h"

namespace mini_lsm {

SSTableBuilder::SSTableBuilder(std::size_t block_size, bool bloom_enabled)
    : block_size_(block_size), bloom_enabled_(bloom_enabled) {}

bool SSTableBuilder::add(KeyView key, ValueView value) {
    if (!data_.empty() && !(last_key_ < key)) return false;
    if (data_.empty()) first_key_ = Key{key};
    last_key_ = Key{key};
    ++num_entries_;
    data_.append(Key{key});
    data_.append(Value{value});
    return true;
}

Status SSTableBuilder::finish(std::uint64_t /*id*/, std::filesystem::path /*path*/,
                              std::unique_ptr<SSTable>& /*out*/) {
    return Status::NotSupported("SSTableBuilder::finish is a S1 TODO");
}

} // namespace mini_lsm