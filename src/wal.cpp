#include "wal.h"

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

std::uint16_t read_u16le(const char* p) {
    return static_cast<std::uint16_t>(
        static_cast<unsigned char>(p[0]) |
        (static_cast<unsigned char>(p[1]) << 8));
}

std::uint32_t read_u32le(const char* p) {
    return static_cast<std::uint32_t>(
        static_cast<unsigned char>(p[0]) |
        (static_cast<unsigned char>(p[1]) << 8) |
        (static_cast<unsigned char>(p[2]) << 16) |
        (static_cast<unsigned char>(p[3]) << 24));
}

std::uint32_t crc32(std::string_view data) {
    uint32_t crc = 0xFFFFFFFFu;
    for (auto c : data) {
        crc ^= static_cast<unsigned char>(c);
        for (int j = 0; j < 8; ++j)
            crc = (crc >> 1) ^ (crc & 1 ? 0xEDB88320u : 0);
    }
    return crc ^ 0xFFFFFFFFu;
}

} // anonymous namespace

Status WAL::open(std::filesystem::path path) {
    path_ = std::move(path);
    out_.open(path_, std::ios::out | std::ios::binary | std::ios::app);
    if (!out_.is_open()) {
        return Status::IOError("cannot open WAL for writing");
    }
    return Status::OK();
}

Status WAL::append(KeyView key, ValueView value) {
    auto klen = static_cast<std::uint16_t>(key.size());
    auto vlen = static_cast<std::uint32_t>(value.size());
    std::uint32_t total = 1 + 2 + klen + 4 + vlen + 4;

    std::string buf;
    buf.reserve(total);
    buf.push_back(static_cast<char>(0xAB));
    write_u16le(buf, klen);
    buf.append(key.data(), klen);
    write_u32le(buf, vlen);
    buf.append(value.data(), vlen);
    auto c = crc32(std::string_view{buf.data() + 1, static_cast<std::size_t>(total - 5)});
    write_u32le(buf, c);

    out_.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    out_.flush();
    return Status::OK();
}

Status WAL::recover(std::vector<std::pair<Key, Value>>& out) const {
    out.clear();
    if (!std::filesystem::exists(path_)) return Status::OK();

    std::ifstream f(path_, std::ios::in | std::ios::binary);
    if (!f.is_open()) {
        return Status::IOError("cannot open WAL for recovery");
    }

    f.seekg(0, std::ios::end);
    auto fsize = static_cast<std::size_t>(f.tellg());
    f.seekg(0, std::ios::beg);

    while (static_cast<std::size_t>(f.tellg()) < fsize) {
        char magic;
        f.read(&magic, 1);
        if (!f) break;
        if (static_cast<unsigned char>(magic) != 0xAB) break;

        char klen_buf[2];
        f.read(klen_buf, 2);
        if (!f) break;
        auto klen = read_u16le(klen_buf);

        std::string key(klen, '\0');
        f.read(key.data(), klen);
        if (!f) break;

        char vlen_buf[4];
        f.read(vlen_buf, 4);
        if (!f) break;
        auto vlen = read_u32le(vlen_buf);

        std::string value(vlen, '\0');
        f.read(value.data(), vlen);
        if (!f) break;

        char crc_buf[4];
        f.read(crc_buf, 4);
        if (!f) break;
        auto stored_crc = read_u32le(crc_buf);

        // Verify CRC over key_len + key + value_len + value
        std::string check;
        check.append(klen_buf, 2);
        check.append(key);
        check.append(vlen_buf, 4);
        check.append(value);
        if (crc32(check) != stored_crc) break;

        out.emplace_back(std::move(key), std::move(value));
    }

    return Status::OK();
}

void WAL::close() {
    if (out_.is_open()) out_.close();
}

} // namespace mini_lsm
