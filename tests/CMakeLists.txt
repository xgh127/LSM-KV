# LSM-KV tests
# Each test file compiles into its own binary so `ctest -R memtable` runs
# ONLY the memtable tests (not all 49 at once).

set(LSM_TEST_FILES
    test_memtable.cpp
    test_sstable.cpp
    test_sstable_builder.cpp
    test_vlog.cpp
    test_iterator.cpp
    test_compaction.cpp
    test_lsm_engine.cpp)

foreach(test_file ${LSM_TEST_FILES})
    string(REGEX REPLACE "test_(.*)\\.cpp" "\\1" test_name "${test_file}")

    add_executable(test_${test_name}
        test_main.cpp
        ${test_file})
    target_link_libraries(test_${test_name} PRIVATE
        mini_lsm
        ${LSM_TEST_LIBS})
    target_include_directories(test_${test_name} PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR})

    if(MINGW)
        target_link_options(test_${test_name} PRIVATE -static-libgcc -static-libstdc++ -static)
    endif()

    add_test(NAME ${test_name} COMMAND test_${test_name})
    set_tests_properties(${test_name} PROPERTIES LABELS "unit;s0;${test_name}")
endforeach()
