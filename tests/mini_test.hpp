// mini_test.hpp
// -----------------------------------------------------------------------------
// A header-only, dependency-free, gtest-flavoured micro test framework.
// Provides just enough surface for TDD in LSM-KV's S0 skeleton:
//     TEST(Suite, Name) { ... }
//     EXPECT_EQ(a, b) / EXPECT_NE / EXPECT_TRUE / EXPECT_FALSE
//     ASSERT_*       (stops the current test on failure)
//     FAIL() / SUCCEED()
//
// Design goals:
//   * No external dep so `cmake -B build && cmake --build build && ctest`
//     works on a pristine machine (no vcpkg, no gtest).
//   * On failure: print "expected vs actual" to stderr, never seg-fault,
//     and let the binary exit non-zero so CTest picks it up.
//   * Switching to real gtest later is mechanical: rename EXPECT_EQ -> gtest's.
//
// Caveats for S0:
//   * Not thread-safe (registry populates during static init).
//   * TEST_F is not supported; use TEST() with manual setup/teardown inside.
//   * No death/SUBSET/etc. — extend when needed.
// -----------------------------------------------------------------------------
#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <string_view>
#include <sstream>
#include <iostream>
#include <vector>
#include <functional>
#include <utility>

namespace mini_test {

// Registry entry ------------------------------------------------------------
struct TestEntry {
    const char* suite;
    const char* name;
    std::function<void()> body;
};

inline std::vector<TestEntry>& Registry() {
    static std::vector<TestEntry> r;
    return r;
}

// RAII registrar instantiated by the TEST macro ------------------------------
struct Registrar {
    Registrar(const char* suite, const char* name, std::function<void()> body) {
        Registry().push_back({suite, name, std::move(body)});
    }
};

// Failure reporter ----------------------------------------------------------
struct Failure {
    std::string file;
    int         line;
    std::string expr;     // empty for unconditional FAIL
    std::string details;  // extra message
    bool        fatal;     // ASSERT_* vs EXPECT_*
};

inline std::vector<Failure>& CurrentFailures() {
    static std::vector<Failure> f;
    return f;
}

inline thread_local bool g_should_abort_current = false;

inline void RecordFailure(std::string file, int line, std::string expr,
                          std::string details, bool fatal) {
    Failure fl{std::move(file), line, std::move(expr), std::move(details), fatal};
    std::fprintf(stderr, "[  FAILED  ] %s:%d: %s\n",
                 fl.file.c_str(), fl.line,
                 fl.expr.empty() ? "(unconditional failure)" : fl.expr.c_str());
    if (!fl.details.empty()) {
        std::fprintf(stderr, "            %s\n", fl.details.c_str());
    }
    CurrentFailures().push_back(std::move(fl));
    if (fatal) {
        g_should_abort_current = true;
        throw std::runtime_error("mini_test: abort current test");
    }
}

inline void ClearCurrentFailures() { CurrentFailures().clear(); }
inline bool HadFailures()           { return !CurrentFailures().empty(); }

// Print a pretty two-line "expected vs actual" -------------------------------
inline void PrintExpectedVsActual(std::string_view file, int line,
                                  std::string_view expr,
                                  std::string_view expected,
                                  std::string_view actual) {
    std::fprintf(stderr, "[  FAILED  ] %.*s:%d: %.*s\n",
                 static_cast<int>(file.size()),   file.data(),
                 line,
                 static_cast<int>(expr.size()),   expr.data());
    std::fprintf(stderr, "            Expected: %.*s\n",
                 static_cast<int>(expected.size()), expected.data());
    std::fprintf(stderr, "            Actual:   %.*s\n",
                 static_cast<int>(actual.size()),   actual.data());
}

// Type printers (extend on demand) -----------------------------------------
template <class T>
inline std::string ToStr(const T& v) {
    std::ostringstream os; os << v; return os.str();
}
inline std::string ToStr(std::nullptr_t)         { return "<nullptr>"; }
inline std::string ToStr(bool b)                 { return b ? "true" : "false"; }
inline std::string ToStr(std::string_view s)    { return std::string(s); }
inline std::string ToStr(const std::string& s)  { return s; }

// Equality helpers ----------------------------------------------------------
template <class A, class B>
inline bool Eq(const A& a, const B& b) { return a == b; }

} // namespace mini_test

// ---------------------------------------------------------------------------
// Macros
// ---------------------------------------------------------------------------
#define MINI_TEST_STRINGIFY(x) #x

#define MINI_TEST_RECORD(file, line, expr, details, fatal)                       \
    ::mini_test::RecordFailure(file, line, expr, details, fatal)

// EXPECT_* ------------------------------------------------------------------
#define MINI_TEST_EXPECT_OP(a, b, op, opname)                                    \
    do {                                                                         \
        auto _a = (a);                                                           \
        auto _b = (b);                                                           \
        if (!(_a op _b)) {                                                        \
            std::string _expr = MINI_TEST_STRINGIFY(a) " " opname                \
                                " " MINI_TEST_STRINGIFY(b);                      \
            std::string _det = "Expected: " + ::mini_test::ToStr(_a) +           \
                               ", Actual: "   + ::mini_test::ToStr(_b);          \
            MINI_TEST_RECORD(__FILE__, __LINE__, _expr, _det, /*fatal=*/false);  \
        }                                                                        \
    } while (0)

#define MINI_TEST_ASSERT_OP(a, b, op, opname)                                    \
    do {                                                                         \
        auto _a = (a);                                                           \
        auto _b = (b);                                                           \
        if (!(_a op _b)) {                                                        \
            std::string _expr = MINI_TEST_STRINGIFY(a) " " opname                \
                                " " MINI_TEST_STRINGIFY(b);                      \
            std::string _det = "Expected: " + ::mini_test::ToStr(_a) +           \
                               ", Actual: "   + ::mini_test::ToStr(_b);          \
            MINI_TEST_RECORD(__FILE__, __LINE__, _expr, _det, /*fatal=*/true);   \
            return;                                                              \
        }                                                                        \
    } while (0)

#define EXPECT_EQ(a, b) MINI_TEST_EXPECT_OP(a, b, ==, "==")
#define EXPECT_NE(a, b) MINI_TEST_EXPECT_OP(a, b, !=, "!=")
#define EXPECT_LT(a, b) MINI_TEST_EXPECT_OP(a, b, < , "<")
#define EXPECT_LE(a, b) MINI_TEST_EXPECT_OP(a, b, <=, "<=")
#define EXPECT_GT(a, b) MINI_TEST_EXPECT_OP(a, b, > , ">")
#define EXPECT_GE(a, b) MINI_TEST_EXPECT_OP(a, b, >=, ">=")

#define ASSERT_EQ(a, b) MINI_TEST_ASSERT_OP(a, b, ==, "==")
#define ASSERT_NE(a, b) MINI_TEST_ASSERT_OP(a, b, !=, "!=")
#define ASSERT_TRUE(c)  MINI_TEST_ASSERT_OP(c, true, ==, "==")
#define ASSERT_FALSE(c) MINI_TEST_ASSERT_OP(c, false, ==, "==")

#define EXPECT_TRUE(c)  MINI_TEST_EXPECT_OP(c, true, ==, "==")
#define EXPECT_FALSE(c) MINI_TEST_EXPECT_OP(c, false, ==, "==")

#define MINI_TEST_BOOL_OP(c, fatal)                                              \
    do {                                                                         \
        if (!(c)) {                                                              \
            MINI_TEST_RECORD(__FILE__, __LINE__, MINI_TEST_STRINGIFY(c),         \
                             "", fatal);                                          \
        }                                                                        \
    } while (0)

#define FAIL() MINI_TEST_RECORD(__FILE__, __LINE__, "", "explicit FAIL()", true)
#define SUCCEED()                                                                 \
    do { /* no-op; placeholder for symmetry */ } while (0)

// TEST(...) -- creates a static registrar ----------------------------------
#define TEST(suite, name)                                                        \
    static void mini_test_##suite##_##name##_body();                            \
    static ::mini_test::Registrar mini_test_##suite##_##name##_registrar(        \
        #suite, #name, &mini_test_##suite##_##name##_body);                      \
    static void mini_test_##suite##_##name##_body()

// (end of mini_test.hpp)