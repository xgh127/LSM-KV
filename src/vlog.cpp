#include "vlog.h"

#include <cstring>
#include <filesystem>

namespace mini_lsm {

namespace {

void write_u16le(std::string& buf, std::uint16_t v) {
    buf.push_back(static_cast<char>(v & 0xFF));
    buf.push_back(static_cast<char>((v >> 8) & 0xFF));
}

void write_u32le(std::string& buf, std::uint32_t v) {
    buf.push_back(static_cast<char>(v & 0xFF));
    buf.push_back(static_cast<char>((v >> 8) & 0xFF));
    buf.push_back(static_cast<char>((v >> 16) & 0xFF));
    buf.push_back(static_cast<char>((v >> 24) & 0xFF));
}

std::uint32_t crc32_checksum(std::string_view data) {
    uint32_t crc = 0xFFFFFFFFu;
    for (auto c : data) {
        crc ^= static_cast<unsigned char>(c);
        for (int j = 0; j < 8; ++j) {
            crc = (crc >> 1) ^ (crc & 1 ? 0xEDB88320u : 0);
        }
    }
    return crc ^ 0xFFFFFFFFu;
}

} // anonymous namespace

Status VLog::open(std::filesystem::path path, bool create_if_missing) {
    path_ = path;

    if (!create_if_missing && !std::filesystem::exists(path_)) {
        return Status::IOError("vlog file does not exist and create_if_missing is false");
    }

    if (create_if_missing) {
        std::error_code ec;
        std::filesystem::create_directories(path_.parent_path(), ec);
        if (ec) {
            return Status::IOError(std::string{"create_directories: "} + ec.message());
        }
    }

    out_.open(path_, std::ios::out | std::ios::binary | std::ios::app);
    if (!out_.is_open()) {
        return Status::IOError("cannot open vlog for writing");
    }
    in_.open(path_, std::ios::in | std::ios::binary);

    next_offset_ = std::filesystem::exists(path_) ? std::filesystem::file_size(path_) : 0;

    return Status::OK();
}

Status VLog::append(KeyView key, ValueView value, VLogHandle& out) {
    // Format: magic(1) + key_len(2) + key + value_len(4) + value + crc32(4)
    std::uint16_t klen = static_cast<std::uint16_t>(key.size());
    std::uint32_t vlen = static_cast<std::uint32_t>(value.size());
    std::uint32_t total = 1 + 2 + klen + 4 + vlen + 4;

    auto offset = next_offset_;
    next_offset_ += total;

    std::string buf;
    buf.reserve(total);
    buf.push_back(static_cast<char>(0xFA));
    write_u16le(buf, klen);
    buf.append(key.data(), klen);
    write_u32le(buf, vlen);
    buf.append(value.data(), vlen);
    auto crc = crc32_checksum(std::string_view{buf.data() + 1, static_cast<std::size_t>(total - 5)});
    write_u32le(buf, crc);

    out_.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    out_.flush();

    out.offset = offset;
    out.length = total;
    return Status::OK();
}

Status VLog::read_at(VLogHandle h, std::string& out) const {
    if (h.length < 7) {
        return Status::IOError("invalid handle: too small");
    }
    std::string buf(h.length, '\0');
    in_.seekg(static_cast<std::streamoff>(h.offset), std::ios::beg);
    if (!in_) {
        return Status::IOError("seekg failed at offset " + std::to_string(h.offset));
    }
    in_.read(buf.data(), static_cast<std::streamsize>(h.length));
    if (!in_) {
        return Status::IOError("short read at offset " + std::to_string(h.offset));
    }

    // Verify magic
    if (static_cast<unsigned char>(buf[0]) != 0xFA) {
        return Status::IOError("bad vlog magic");
    }

    // Parse header
    auto klen = static_cast<std::uint16_t>(
        static_cast<unsigned char>(buf[1]) |
        (static_cast<unsigned char>(buf[2]) << 8));

    if (static_cast<std::size_t>(1 + 2 + klen + 4) > h.length) {
        return Status::IOError("vlog record truncated");
    }

    auto vlen = static_cast<std::uint32_t>(
        static_cast<unsigned char>(buf[3 + klen]) |
        (static_cast<unsigned char>(buf[4 + klen]) << 8) |
        (static_cast<unsigned char>(buf[5 + klen]) << 16) |
        (static_cast<unsigned char>(buf[6 + klen]) << 24));

    if (static_cast<std::size_t>(1 + 2 + klen + 4 + vlen + 4) > h.length) {
        return Status::IOError("vlog value truncated");
    }

    // Verify CRC
    auto data_start = 1;
    auto data_len = static_cast<std::size_t>(2 + klen + 4 + vlen);
    auto stored_crc = static_cast<std::uint32_t>(
        static_cast<unsigned char>(buf[1 + 2 + klen + 4 + vlen]) |
        (static_cast<unsigned char>(buf[2 + 2 + klen + 4 + vlen]) << 8) |
        (static_cast<unsigned char>(buf[3 + 2 + klen + 4 + vlen]) << 16) |
        (static_cast<unsigned char>(buf[4 + 2 + klen + 4 + vlen]) << 24));

    auto computed = crc32_checksum(
        std::string_view{buf.data() + data_start, data_len});
    if (computed != stored_crc) {
        return Status::IOError("vlog CRC mismatch");
    }

    // Extract value
    auto value_start = static_cast<std::size_t>(1 + 2 + klen + 4);
    out = buf.substr(value_start, vlen);
    return Status::OK();
}

Status VLog::gc(std::uint64_t /*chunk_size*/, std::uint64_t& reclaimed) {
    reclaimed = 0;
    return Status::OK();
}

std::uint64_t VLog::size_bytes() const {
    return next_offset_;
}

} // namespace mini_lsm
