set(TESTS test_iht_fast test_iht_small test_iht_large)

foreach(spec ${TESTS})
    add_executable(${spec} ${spec}.c)
    target_compile_options(${spec} PRIVATE -march=native -Wall -Wextra  -Werror)

    target_link_libraries(${spec} PRIVATE index-hash-table m)

    target_compile_definitions(${spec} PRIVATE _DEFAULT_SOURCE _POSIX_C_SOURCE=200809L)
    target_include_directories(${spec} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

endforeach()
add_test(NAME test_iht_fast      COMMAND test_iht_fast -s)
add_test(NAME test_iht_fast_1k   COMMAND test_iht_fast -n1000 -r10000)
add_test(NAME test_iht_fast_10k  COMMAND test_iht_fast -n10000 -r1000)
add_test(NAME test_iht_fast_100k COMMAND test_iht_fast -n100000 -r100)

add_test(NAME test_iht_small COMMAND test_iht_small)
add_test(NAME test_iht_large COMMAND test_iht_large)
