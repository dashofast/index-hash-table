cmake_minimum_required(VERSION 3.10.0)
project(index-hash-table VERSION 0.1.0 LANGUAGES C)

add_library(index-hash-table src/index-hash-table.c)

target_compile_features(index-hash-table PRIVATE c_std_23)
target_compile_options(index-hash-table PRIVATE -march=native)

target_include_directories(index-hash-table
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src    
)

include(CTest)
enable_testing()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CPACK_PROJECT_NAME ${PROJECT_NAME})
set(CPACK_PROJECT_VERSION ${PROJECT_VERSION})
include(CPack)
