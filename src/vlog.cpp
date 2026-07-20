// vlog.cpp
// -----------------------------------------------------------------------------
// S0 stub of VLog. Tests use it to assert:
//   * open(path, create_if_missing=true) creates parent dirs and the vlog file
//   * append_to_tail owns an O_APPEND handle so offsets are monotonic
//   * read_at(offset, len) round-trips the same bytes that were appended
//   * gc(chunk_size) is callable and returns Status::OK even as a no-op
//
// Implementation suggestion (S0 root): std::ofstream ios::app + binary;
// for read-back open std::ifstream lazily on first read (kept open here for
// simplicity). Maintain in-memory `next_offset_` counter.
// -----------------------------------------------------------------------------
#include "vlog.h"

#include <filesystem>

namespace mini_lsm {

Status VLog::open(std::filesystem::path path, bool create_if_missing) {
    path_ = path;

    if (create_if_missing) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return Status::IOError(std::string{"create_directories: "} + ec.message());
        }
    }

    // Open for append-only writes; create file if missing.
    out_.open(path_, std::ios::out | std::ios::binary | std::ios::app);
    if (!out_.is_open()) {
        return Status::IOError("cannot open vlog for writing");
    }
    in_.open(path_, std::ios::in | std::ios::binary);
    // Reading from an empty/brand-new file is fine; in_ stays not-open until writes happen.
    std::error_code ec;
    next_offset_ = std::filesystem::exists(path_) ? std::filesystem::file_size(path_) : 0;
    (void)ec;

    return Status::OK();
}

Status VLog::append(KeyView /*key*/, ValueView /*value*/, VLogHandle& /*out*/) {
    // TODO(S1): write a header [magic|key_len|key|val_len] << value << crc32;
    // record out.offset = next_offset_; advance next_offset_ by total written.
    // S0: leave as no-op.
    return Status::OK();
}

Status VLog::read_at(VLogHandle /*h*/, std::string& /*out*/) const {
    // TODO(S1): in_.seekg(h.offset); read(h.length); verify CRC.
    return Status::NotSupported("VLog::read_at is a S1 TODO");
}

Status VLog::gc(std::uint64_t /*chunk_size*/, std::uint64_t& reclaimed) {
    // TODO(S2+): WiscKey GC. S0 no-op.
    reclaimed = 0;
    return Status::OK();
}

std::uint64_t VLog::size_bytes() const {
    return next_offset_;
}

} // namespace mini_lsm