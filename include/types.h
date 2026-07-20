// types.h
// -----------------------------------------------------------------------------
// Public type aliases shared across LSM-KV.
//
// We deliberately use std::string (not std::vector<std::byte> and not uint64_t)
// for keys and values in the S0 skeleton:
//   * std::string keeps the API ergonomically simple for the tutorial phase.
//   * It carries binary data transparently (std::string may contain NUL).
//   * Future stages (S3 MVCC, S4 zero-copy) can swap the typedef for a
//     Bytes type without touching call sites — all subsystems below go through
//     std::string_view at the entry.
// -----------------------------------------------------------------------------
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace mini_lsm {

// Key / Value carrier -------------------------------------------------------
// Note: Key carries binary bytes; do not assume NUL-termination.
// Note: Value of empty std::string ("") is reserved as the *tombstone*
// sentinel throughout the engine — put(key, "") is equivalent to del(key).
using Key   = std::string;
using Value = std::string;

// Read-only views used in iterator/value APIs.
using KeyView   = std::string_view;
using ValueView = std::string_view;

// Status --------------------------------------------------------------------
// Minimal status object — improves on bool because it carries a message.
// C++23 std::expected will eventually replace Result<T> hand-roll below.
class Status {
public:
    enum class Code { kOk, kNotFound, kCorruption, kIOError, kInvalidArgument, kNotSupported };

    Status() = default;
    Status(Code c, std::string msg) : code_(c), msg_(std::move(msg)) {}

    static Status OK()                    { return {}; }
    static Status NotFound(std::string m) { return {Code::kNotFound,        std::move(m)}; }
    static Status Corruption(std::string m) { return {Code::kCorruption,   std::move(m)}; }
    static Status IOError(std::string m)    { return {Code::kIOError,      std::move(m)}; }
    static Status InvalidArgument(std::string m) { return {Code::kInvalidArgument, std::move(m)}; }
    static Status NotSupported(std::string m)    { return {Code::kNotSupported,    std::move(m)}; }

    bool ok()        const { return code_ == Code::kOk; }
    bool not_found() const { return code_ == Code::kNotFound; }
    Code code()      const { return code_; }
    const std::string& message() const { return msg_; }

private:
    Code        code_ = Code::kOk;
    std::string msg_;
};

} // namespace mini_lsm