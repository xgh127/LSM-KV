# LSM-KV tests
#
# Each test file mirrors one logical component and is registered as an
# independent CTest entry so `ctest -R memtable` etc. work naturally.

set(LSM_TEST_FILES
    test_memtable.cpp
    test_sstable.cpp
    test_sstable_builder.cpp
    test_vlog.cpp
    test_iterator.cpp
    test_compaction.cpp
    test_lsm_engine.cpp)

# We compile every test file into one binary so that the self-host
# mini_test framework (single registration mechanism across TUs) works
# without external machinery. Each TEST(...) registers itself statically.
add_executable(mini_lsm_tests
    test_main.cpp
    ${LSM_TEST_FILES})
target_link_libraries(mini_lsm_tests PRIVATE
    mini_lsm
    ${LSM_TEST_LIBS})
target_include_directories(mini_lsm_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR})

# Static-link MinGW runtime DLLs — see top-level CMakeLists.txt comment.
if(MINGW)
    target_link_options(mini_lsm_tests PRIVATE -static-libgcc -static-libstdc++ -static)
endif()

# Run tests from build dir itself; mini_test prints EXPECT failures on
# stdout/stderr but the process exit code reflects pass/fail count so CTest
# can pick up failures without SegFault.
add_test(NAME mini_lsm_all_tests COMMAND mini_lsm_tests)

# Convenience label so `ctest -L unit` works as documented in PLAN.md
set_tests_properties(mini_lsm_all_tests PROPERTIES LABELS "unit")

# Per-component pseudo labels via environment-style properties
# (so that `ctest -L memtable` does something meaningful for S0).
set_tests_properties(mini_lsm_all_tests PROPERTIES
    LABELS "unit;s0;memtable;sstable;vlog;iterator;compaction;engine")