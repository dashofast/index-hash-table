set(TESTS test_iht_fast test_iht_small test_iht_large test_iht_fast_10k test_iht_fast_100k)

foreach(spec ${TESTS})
    add_executable(${spec} ${spec}.c)

    target_link_libraries(${spec} PRIVATE index-hash-table m)

    target_compile_definitions(${spec} PRIVATE _DEFAULT_SOURCE _POSIX_C_SOURCE=200809L)
    target_include_directories(${spec} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

    add_test(NAME ${spec} COMMAND ${spec})
endforeach()
