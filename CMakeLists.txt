cmake_minimum_required(VERSION 3.25)
project(VeriObjC LANGUAGES C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Point to CppUTest installation (override via -DCPPUTEST_ROOT=... or CPPUTEST_ROOT env var)
if(DEFINED ENV{CPPUTEST_ROOT})
    set(DEFAULT_CPPUTEST_ROOT "$ENV{CPPUTEST_ROOT}")
else()
    get_filename_component(DEFAULT_CPPUTEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../cpputest" ABSOLUTE)
endif()
set(CPPUTEST_ROOT "${DEFAULT_CPPUTEST_ROOT}" CACHE PATH "Path to CppUTest root")
set(CPPUTEST_INCLUDE_DIRS "${CPPUTEST_ROOT}/include")
set(CPPUTEST_LIB "${CPPUTEST_ROOT}/cpputest_build/src/CppUTest/libCppUTest.a")
set(CPPUTEST_EXT_LIB "${CPPUTEST_ROOT}/cpputest_build/src/CppUTestExt/libCppUTestExt.a")

# Per-component INTERFACE targets enforcing the Phase 3 Layer Table:
#   rt   -> (nothing)
#   vstd -> rt, vstd
#   sys0 -> rt, sys0
#   hw   -> rt, sys0, hw
add_library(veriobjc_rt INTERFACE)
target_include_directories(veriobjc_rt INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/rt/include
)

add_library(veriobjc_vstd INTERFACE)
target_include_directories(veriobjc_vstd INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/vstd/include
)
target_link_libraries(veriobjc_vstd INTERFACE veriobjc_rt)

add_library(veriobjc_sys0 INTERFACE)
target_include_directories(veriobjc_sys0 INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/sys0/include
)
target_link_libraries(veriobjc_sys0 INTERFACE veriobjc_rt)

add_library(veriobjc_hw INTERFACE)
target_include_directories(veriobjc_hw INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/hw/include
)
target_link_libraries(veriobjc_hw INTERFACE veriobjc_sys0 veriobjc_rt)

enable_testing()

# Unit tests for micro-runtime and vstd / sys0 substrates
add_executable(unit_rt_tests
    tests/unit_rt/main.cpp
    tests/unit_rt/TestStackAlloc.cpp
    tests/unit_rt/TestPoolAlloc.cpp
    tests/unit_rt/TestGenPool.cpp
    tests/unit_rt/TestScopeAndLayout.cpp
    tests/unit_rt/TestDispatch.cpp
    tests/unit_rt/TestTapeAndRelPtr.cpp
    tests/unit_rt/TestVstdWave1.cpp
    tests/unit_rt/TestVstdWave2And3.cpp
    tests/unit_rt/TestVstdWave4And5.cpp
    tests/unit_rt/TestVtpeV1GoldenCorpus.cpp
    tests/unit_rt/TestVeriStreamRuntimeSlice.cpp
)

target_compile_definitions(unit_rt_tests PRIVATE
    VERIOBJC_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
)

# Compile all vstd unit tests as Objective-C++ so the #ifdef __OBJC__ halves of
# all <vstd/*.h> headers are exercised and measured by llvm-cov.
set_source_files_properties(
    tests/unit_rt/TestVstdWave1.cpp
    tests/unit_rt/TestVstdWave2And3.cpp
    tests/unit_rt/TestVstdWave4And5.cpp
    PROPERTIES COMPILE_OPTIONS "-x;objective-c++;-fno-exceptions"
)

target_link_libraries(unit_rt_tests PRIVATE
    veriobjc_rt
    veriobjc_vstd
    veriobjc_sys0
    veriobjc_hw
)

target_include_directories(unit_rt_tests SYSTEM PRIVATE
    ${CPPUTEST_INCLUDE_DIRS}
)

target_compile_options(unit_rt_tests PRIVATE
    -fprofile-instr-generate
    -fcoverage-mapping
)

target_link_options(unit_rt_tests PRIVATE
    -fprofile-instr-generate
    -fcoverage-mapping
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

# Test target for Phase 3 Header Cohesion & Layer Table Gate
add_test(NAME vstd_cohesion_tests
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/tests/integration/check_vstd_cohesion.sh ${CMAKE_CURRENT_SOURCE_DIR}
)
