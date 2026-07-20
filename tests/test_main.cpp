// test_main.cpp
// -----------------------------------------------------------------------------
// Entry point for the single self-hosted mini_test binary used in S0.
//
// `cmake --build build && ctest --test-dir build --output-on-failure`
// invokes this binary; on failure it exits non-zero (so CTest reports failure)
// but it *never* crashes — unimplemented starter code is expected to make
// assertions fail gracefully, which is the whole TDD "red" stage.
// -----------------------------------------------------------------------------
#include "mini_test.hpp"

#include <cstdio>
#include <exception>

int main() {
    // Emit a startup marker on stderr so we can detect hang before any
    // buffering kicks in. Remove once S0 stabilises.
    std::fprintf(stderr, "[mini_test] startup, %zu cases registered\n",
                 ::mini_test::Registry().size());
    std::fflush(stderr);

    std::printf("[==========] Running %zu mini_test cases\n",
                ::mini_test::Registry().size());
    std::fflush(stdout);
    int passed = 0, failed = 0;

    for (const auto& e : ::mini_test::Registry()) {
        ::mini_test::ClearCurrentFailures();
        ::mini_test::g_should_abort_current = false;

        std::printf("[ RUN      ] %s.%s\n", e.suite, e.name);
        try {
            e.body();
        } catch (const std::exception& ex) {
            ::mini_test::RecordFailure(__FILE__, __LINE__,
                "uncaught std::exception", ex.what(), /*fatal=*/false);
        } catch (...) {
            ::mini_test::RecordFailure(__FILE__, __LINE__,
                "uncaught unknown exception", "", /*fatal=*/false);
        }

        if (::mini_test::HadFailures()) {
            ++failed;
            std::printf("[  FAILED  ] %s.%s\n", e.suite, e.name);
        } else {
            ++passed;
            std::printf("[       OK ] %s.%s\n", e.suite, e.name);
        }
    }

    std::printf("[==========] %d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}