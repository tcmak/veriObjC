cmake_minimum_required(VERSION 3.25)
project(VeriObjC LANGUAGES C CXX)

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 26)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Discover CppUTest (override via -DCPPUTEST_ROOT=... or CPPUTEST_ROOT env var)
include(CTest)
if(NOT CPPUTEST_ROOT)
    if(DEFINED ENV{CPPUTEST_ROOT} AND NOT "$ENV{CPPUTEST_ROOT}" STREQUAL "")
        set(DEFAULT_CPPUTEST_ROOT "$ENV{CPPUTEST_ROOT}")
    elseif(DEFINED ENV{CPPUTEST_DIR} AND NOT "$ENV{CPPUTEST_DIR}" STREQUAL "")
        set(DEFAULT_CPPUTEST_ROOT "$ENV{CPPUTEST_DIR}")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../cpputest")
        get_filename_component(DEFAULT_CPPUTEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../cpputest" ABSOLUTE)
    elseif(DEFINED ENV{HOME} AND EXISTS "$ENV{HOME}/.veriobjc/cpputest")
        set(DEFAULT_CPPUTEST_ROOT "$ENV{HOME}/.veriobjc/cpputest")
    else()
        get_filename_component(DEFAULT_CPPUTEST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../cpputest" ABSOLUTE)
    endif()
    set(CPPUTEST_ROOT "${DEFAULT_CPPUTEST_ROOT}" CACHE PATH "Path to CppUTest root")
endif()

find_path(CPPUTEST_INCLUDE_DIRS
    NAMES CppUTest/TestHarness.h
    HINTS "${CPPUTEST_ROOT}"
    PATH_SUFFIXES include
)
find_library(CPPUTEST_LIB
    NAMES CppUTest
    HINTS "${CPPUTEST_ROOT}"
    PATH_SUFFIXES
        cpputest_build/src/CppUTest
        build/src/CppUTest
        cpputest_build/lib
        build/lib
        lib
)
find_library(CPPUTEST_EXT_LIB
    NAMES CppUTestExt
    HINTS "${CPPUTEST_ROOT}"
    PATH_SUFFIXES
        cpputest_build/src/CppUTestExt
        build/src/CppUTestExt
        cpputest_build/lib
        build/lib
        lib
)

# Per-component INTERFACE targets enforcing the Phase 3 Layer Table:
#   rt   -> (nothing)
#   vstd -> rt, vstd
#   sys0 -> rt, sys0
#   hw   -> rt, sys0, hw
#   host -> rt, host
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

add_library(veriobjc_host INTERFACE)
target_include_directories(veriobjc_host INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/host/include
)
target_link_libraries(veriobjc_host INTERFACE veriobjc_rt)

if(BUILD_TESTING)
    if(NOT CPPUTEST_INCLUDE_DIRS OR NOT CPPUTEST_LIB OR NOT CPPUTEST_EXT_LIB)
        message(FATAL_ERROR
            "CppUTest not found (CPPUTEST_ROOT='${CPPUTEST_ROOT}'). "
            "Run 'bash scripts/install.sh', pass -DCPPUTEST_ROOT=/path/to/cpputest, "
            "or disable unit tests with -DBUILD_TESTING=OFF."
        )
    endif()

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
        tests/unit_rt/TestBenchHarness.cpp
        tests/unit_rt/TestSurvivorReport.cpp
        tests/unit_rt/TestLatencyBudgetGate.cpp
        tests/unit_rt/TestSmtContractBridge.cpp
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
        veriobjc_host
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
endif()

# ---------------------------------------------------------------------------
# Clang AST Frontend Plugin (libVeriObjCPlugin.so)
# ---------------------------------------------------------------------------

# Discover LLVM/Clang portably (override via -DLLVM_DIR=/path/to/lib/cmake/llvm)
set(_VERIOBJC_LLVM_HINTS "")
if(CMAKE_CXX_COMPILER)
    get_filename_component(_VERIOBJC_CXX_REAL "${CMAKE_CXX_COMPILER}" REALPATH)
    get_filename_component(_VERIOBJC_CXX_BIN "${_VERIOBJC_CXX_REAL}" DIRECTORY)
    get_filename_component(_VERIOBJC_CXX_PREFIX "${_VERIOBJC_CXX_BIN}" DIRECTORY)
    list(APPEND _VERIOBJC_LLVM_HINTS
        "${_VERIOBJC_CXX_PREFIX}/lib/cmake/llvm"
        "${_VERIOBJC_CXX_PREFIX}/cmake"
    )
endif()

find_package(LLVM REQUIRED CONFIG HINTS ${_VERIOBJC_LLVM_HINTS})
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION} via ${LLVM_DIR}")
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})

find_package(Clang CONFIG HINTS
    "${LLVM_DIR}/../clang"
    "${LLVM_LIBRARY_DIRS}/cmake/clang"
)

add_library(VeriObjCPlugin MODULE
    compiler/src/PluginMain.cpp
    compiler/src/AstBanChecks.cpp
    compiler/src/CfgDataflowChecks.cpp
    compiler/src/StaticDispatchLowering.cpp
    compiler/src/ContainerChecks.cpp
)

target_include_directories(VeriObjCPlugin PRIVATE
    compiler/include
)

target_include_directories(VeriObjCPlugin SYSTEM PRIVATE
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)

target_compile_definitions(VeriObjCPlugin PRIVATE
    ${LLVM_DEFINITIONS_LIST}
)

target_compile_options(VeriObjCPlugin PRIVATE
    -fno-rtti
    -Wno-deprecated-this-capture
)

target_link_directories(VeriObjCPlugin PRIVATE
    ${LLVM_LIBRARY_DIRS}
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
