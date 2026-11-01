cmake_minimum_required(VERSION 3.20)
project(VeriObjC LANGUAGES C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Point to CppUTest installation
set(CPPUTEST_ROOT "/home/user/cpputest" CACHE PATH "Path to CppUTest root")
set(CPPUTEST_INCLUDE_DIRS "${CPPUTEST_ROOT}/include")
set(CPPUTEST_LIB "${CPPUTEST_ROOT}/cpputest_build/src/CppUTest/libCppUTest.a")
set(CPPUTEST_EXT_LIB "${CPPUTEST_ROOT}/cpputest_build/src/CppUTestExt/libCppUTestExt.a")

# Public include directories for runtime
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/rt/include
)

enable_testing()

# Unit tests for micro-runtime
add_executable(unit_rt_tests
    tests/unit_rt/main.cpp
    tests/unit_rt/TestStackAlloc.cpp
    tests/unit_rt/TestPoolAlloc.cpp
    tests/unit_rt/TestGenPool.cpp
)

target_include_directories(unit_rt_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/rt/include
    ${CPPUTEST_INCLUDE_DIRS}
)

target_link_libraries(unit_rt_tests PRIVATE
    ${CPPUTEST_LIB}
    ${CPPUTEST_EXT_LIB}
    pthread
)

add_test(NAME unit_rt_tests COMMAND unit_rt_tests)
