# 🛠️ VeriObjC Build & Execution Guide (`BUILDING.md`)

This guide provides step-by-step instructions on how to configure, build, test, and use the **VeriObjC** toolchain and runtime directly from your command line.

---

## 📋 Prerequisites

VeriObjC requires:
* **Clang / LLVM 23** (`clang-23`, `clang++-23`, `llvm-objdump-23`)
* **CMake 3.25+**
* **Make**
* **CppUTest** (located at `/home/user/cpputest`)

Verify your environment:
```bash
clang-23 --version
cmake --version
```

---

## 🚀 Quick Start (Build & Test Everything)

Run the following commands from the repository root (`/home/user/veriObjC`):

```bash
# 1. Configure the CMake build directory
cmake -B build -G "Unix Makefiles" -DCMAKE_C_COMPILER=clang-23 -DCMAKE_CXX_COMPILER=clang++-23

# 2. Build all targets (micro-runtime tests & Clang plugin)
cmake --build build

# 3. Run all test suites
ctest --test-dir build --output-on-failure
```

---

## 🧪 Running Individual Test Suites

### 1. Freestanding Micro-Runtime CppUTest Suite (`unit_rt_tests`)
Runs all unit tests for the data-stack bump allocator (`veriobjc_stack`), reusable freelist pool (`veriobjc_pool`), generation-counted handles (`vweak`), and scoped cleanup hooks:

```bash
# Run all CppUTest tests
./build/unit_rt_tests

# Run with verbose test-by-test output
./build/unit_rt_tests -v

# Run only a specific test group
./build/unit_rt_tests -sg VeriObjC_GenPool
./build/unit_rt_tests -sg VeriObjC_DataStack
./build/unit_rt_tests -sg VeriObjC_Pool
./build/unit_rt_tests -sg VeriObjC_ScopeAndLayout
./build/unit_rt_tests -sg VeriObjC_Dispatch
./build/unit_rt_tests -sg VeriObjC_TapeAndRelPtr
```

### 2. Clang 23 AST Plugin Test Suite (`plugin_tests`)
Executes the positive test cases (verifying `0` calls to `objc_msgSend`, `objc_msg_lookup`, `objc_enumerationMutation`, `_NSConcreteStackBlock`, or `_NSConcreteGlobalBlock`, and verifying `.rodata` DFA table emission and range-for pointer lowering) and the zero-bypass negative rejection suite (verifying compilation aborts on `id`, `NSObject`, dynamic reflection, untyped messages, unresolved selectors, unsealed hierarchies, double-free/use-after-move, escaping stack references, non-flat channel payloads, unbounded range loops, uncaught multi-frame error returns, escaping blocks, unconstrained generics, `vconstexpr` fuel exhaustion, and regex backreferences/lookarounds):

```bash
./tests/plugin/run_plugin_tests.sh build/libVeriObjCPlugin.so .
```

### 3. Examples Smoke & Acceptance Test Suite (`examples_smoke_tests`)
Compiles all `.m` (`audio_pipeline.m`, `sealed_synth_hierarchy.m`, `linear_channel_router.m`, `simd_range_filter.m`, `multiframe_packet_parser.m`, `non_escaping_block_pipeline.m`, `constrained_generic_vault.m`, `vconstexpr_tick_table.m`, `fix_protocol_dfa_router.m`) and `.c` (`kernel_dma_batch.c`, `order_book_snapshot.c`) files in `examples/` with `-O2 -Werror -fblocks` and the Clang plugin, auditing emitted `.o` binaries via `nm` for zero dynamic dispatch calls, zero block runtime dependencies, and zero external `libc` dependencies:

```bash
./tests/integration/run_examples_smoke_test.sh build/libVeriObjCPlugin.so .
```

---

## ⚡ Compiling Code with the VeriObjC Toolchain

### 1. Compiling with the Clang Plugin Activated
To compile any Objective-C file while enforcing all VeriObjC language rules and ban filters (e.g., our audio DSP pipeline example):

```bash
clang-23 -Xclang -load -Xclang build/libVeriObjCPlugin.so \
         -Xclang -add-plugin -Xclang veriobjc-check \
         -I./rt/include \
         -c examples/audio_pipeline.m -o build/audio_pipeline.o
```

* If the code attempts to use `id`, inherit from `NSObject`, call `NSClassFromString`, or send messages to an untyped receiver, **compilation instantly halts** with a fatal diagnostic.

### 2. Compiling Freestanding Bare-Metal Binaries (Zero Libc / Zero OS)
To compile kernel or bare-metal modules without linking against standard OS libraries (e.g., our kernel DMA batch processor example):

```bash
clang-23 -O2 -ffreestanding -nostdlib \
         -I./rt/include \
         -c examples/kernel_dma_batch.c -o build/kernel_dma_batch.o
```

---

## 🔍 Auditing Machine Code ("No Number, No Claim")

VeriObjC enforces mathematical and architectural guarantees down to the emitted machine instructions. You can audit compiled objects directly from your terminal:

### 1. Verify Zero OS / Libc Dependencies (`nm`)
Ensure that your compiled object has **zero undefined external symbols** (`U`), proving zero hidden calls to `malloc`, `free`, or `libc`:

```bash
nm build/lib/your_module.o | grep "U "
# (Expected output: empty — no undefined references)
```

### 2. Verify Single-Store Scope Reset Disassembly (`llvm-objdump`)
Audit that scope teardown compiles to exactly **one `mov` store instruction** rather than calling destructors or walking linked lists:

```bash
llvm-objdump-23 -d build/veriobjc_runtime.o --disassemble-symbols=veriobjc_runtime_scope_guard_test
```
You will see:
```x86asm
movq %rcx, 0x8(%rax)   # One single store: stack->top = mark
retq
```

---

## 📊 Measuring Branch Coverage (`llvm-cov`)

To run the LLVM source-based branch coverage audit:

```bash
# 1. Compile with profiling enabled
clang++-23 -O0 -fprofile-instr-generate -fcoverage-mapping -std=c++26 \
    -Irt/include -isystem /home/user/cpputest/include \
    tests/unit_rt/main.cpp tests/unit_rt/TestStackAlloc.cpp \
    tests/unit_rt/TestPoolAlloc.cpp tests/unit_rt/TestGenPool.cpp \
    tests/unit_rt/TestScopeAndLayout.cpp tests/unit_rt/TestDispatch.cpp \
    tests/unit_rt/TestTapeAndRelPtr.cpp \
    /home/user/cpputest/cpputest_build/src/CppUTest/libCppUTest.a \
    /home/user/cpputest/cpputest_build/src/CppUTestExt/libCppUTestExt.a \
    -lpthread -o build/cov_tests

# 2. Run tests to produce raw profile
LLVM_PROFILE_FILE="build/cov.profraw" ./build/cov_tests >/dev/null

# 3. Merge profile data
llvm-profdata-23 merge -sparse build/cov.profraw -o build/cov.profdata

# 4. View line and branch coverage report
llvm-cov-23 show ./build/cov_tests -instr-profile=build/cov.profdata \
    rt/include/veriobjc_runtime.h --show-branches=count
```

---

## 📂 Repository Artifacts

| Artifact | Location | Purpose |
| :--- | :--- | :--- |
| **Micro-Runtime Header** | `rt/include/veriobjc_runtime.h` | Freestanding data stacks, pools, and object layouts |
| **Clang Plugin** | `build/libVeriObjCPlugin.so` | Clang 23 AST verification pass (`veriobjc-check`) |
| **Unit Test Binary** | `build/unit_rt_tests` | CppUTest runner for micro-runtime |
| **Plugin Test Runner** | `tests/plugin/run_plugin_tests.sh` | Automated positive/negative AST compiler test script |
| **Execution Plan** | `plan.md` | Living checklist of completed and upcoming tasks |
