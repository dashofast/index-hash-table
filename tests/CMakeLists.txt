add_executable(test_iht1 test-iht1.c)

target_link_libraries(test_iht1 PRIVATE index-hash-table m)

target_compile_definitions(test_iht1 PRIVATE _DEFAULT_SOURCE _POSIX_C_SOURCE=200809L)
target_include_directories(test_iht1 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

add_test(NAME IndexHashTableTests COMMAND test_iht1)
