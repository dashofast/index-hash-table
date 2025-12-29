add_executable(test_index_hash_table test1.c)

target_compile_features(index-hash-table PRIVATE c_std_23)

target_link_libraries(test_index_hash_table PRIVATE index-hash-table m)

target_compile_definitions(test_index_hash_table PRIVATE _DEFAULT_SOURCE _POSIX_C_SOURCE=200809L)
target_include_directories(test_index_hash_table PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)

add_test(NAME IndexHashTableTests COMMAND test_index_hash_table)
