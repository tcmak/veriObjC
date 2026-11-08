cmake_minimum_required(VERSION 3.25)
project(VeriObjC LANGUAGES C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 26)
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
    tests/unit_rt/TestScopeAndLayout.cpp
    tests/unit_rt/TestDispatch.cpp
    tests/unit_rt/TestTapeAndRelPtr.cpp
)

target_include_directories(unit_rt_tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/rt/include
)

target_include_directories(unit_rt_tests SYSTEM PRIVATE
    ${CPPUTEST_INCLUDE_DIRS}
)

target_link_libraries(unit_rt_tests PRIVATE
    ${CPPUTEST_LIB}
    ${CPPUTEST_EXT_LIB}
    pthread
)

add_test(NAME unit_rt_tests COMMAND unit_rt_tests)

# ---------------------------------------------------------------------------
# Clang AST Frontend Plugin (libVeriObjCPlugin.so)
# ---------------------------------------------------------------------------

add_library(VeriObjCPlugin MODULE
    compiler/src/VeriObjCPlugin.cpp
)

target_include_directories(VeriObjCPlugin PRIVATE
    compiler/include
)

target_include_directories(VeriObjCPlugin SYSTEM PRIVATE
    /usr/lib/llvm-23/include
)

target_compile_options(VeriObjCPlugin PRIVATE
    -fno-rtti
    -Wno-deprecated-this-capture
)

target_link_directories(VeriObjCPlugin PRIVATE
    /usr/lib/llvm-23/lib
)

target_link_libraries(VeriObjCPlugin PRIVATE
    clang-cpp
)

set_target_properties(VeriObjCPlugin PROPERTIES
    PREFIX "lib"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

# Test target for plugin verification script
add_test(NAME plugin_tests
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/plugin/run_plugin_tests.sh ${CMAKE_BINARY_DIR}/libVeriObjCPlugin.so ${CMAKE_CURRENT_SOURCE_DIR}
)

# Test target for examples smoke & acceptance suite
add_test(NAME examples_smoke_tests
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/run_examples_smoke_test.sh ${CMAKE_BINARY_DIR}/libVeriObjCPlugin.so ${CMAKE_CURRENT_SOURCE_DIR}
)

# Test target for dual-direction Valid-ObjC Ban Rejection Corpus
add_test(NAME ban_corpus_tests
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/run_ban_corpus_test.sh ${CMAKE_BINARY_DIR}/libVeriObjCPlugin.so ${CMAKE_CURRENT_SOURCE_DIR}
)
