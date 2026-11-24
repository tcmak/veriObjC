# 🛠️ VeriObjC Build & Execution Guide (`BUILDING.md`)

This guide provides step-by-step instructions on how to configure, build, test, and use the **VeriObjC** toolchain and runtime directly from your command line.

---

## 📋 Prerequisites

VeriObjC requires:
* **Clang / LLVM 23** (`clang-23`, `clang++-23`, `llvm-objdump-23`; LLVM CMake package discovered via `find_package(LLVM REQUIRED CONFIG)`, configurable with `-DLLVM_DIR=...`)
* **CMake 3.25+**
* **Make**
* **Z3 ≥ 4.8.12 (64-bit)** (`z3`, discovered via `find_program(Z3_EXECUTABLE NAMES z3)`, configurable via `-DZ3_EXECUTABLE=/path/to/z3` or bypassed at configure time with `-DVERIOBJC_REQUIRE_Z3=OFF`; host/compile-time tool only — zero runtime dependency in compiled targets)
* **CppUTest** (configurable via `-DCPPUTEST_ROOT=...` or `CPPUTEST_ROOT`, defaulting to `../cpputest` or `$HOME/.veriobjc/cpputest`; supports both `cpputest_build/` and `build/` layouts)

Verify your environment:
```bash
clang-23 --version
z3 --version
cmake --version
```

---

## 🚀 Quick Start (Build & Test Everything)

Run the following commands from the repository root:

```bash
# 1. Configure the CMake build directory
cmake -B build -G "Unix Makefiles" -DCMAKE_C_COMPILER=clang-23 -DCMAKE_CXX_COMPILER=clang++-23

# 2. Build all targets (micro-runtime archive, unit tests, Clang plugin & build/bin/veriobjc-cc)
cmake --build build

# 3. Run all 7 test suites
ctest --test-dir build --output-on-failure

# 4. (Optional) Install a self-contained, relocatable VeriObjC SDK prefix
cmake --install build --prefix "$HOME/.local"
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
Compiles all `.m` (`audio_pipeline.m`, `sealed_synth_hierarchy.m`, `linear_channel_router.m`, `simd_range_filter.m`, `multiframe_packet_parser.m`, `non_escaping_block_pipeline.m`, `constrained_generic_vault.m`, `vconstexpr_tick_table.m`, `fix_protocol_dfa_router.m`, `ternary_glu_block.m`) and `.c` (`kernel_dma_batch.c`, `order_book_snapshot.c`) files in `examples/` with `-O2 -Werror -fblocks` and the Clang plugin (including compile-time `objc_hw_kernel` zero-multiplication verification), auditing emitted `.o` binaries via `nm` for zero dynamic dispatch calls, zero block runtime dependencies, and zero external `libc` dependencies:

```bash
./tests/integration/run_examples_smoke_test.sh build/libVeriObjCPlugin.so .
```

### 4. Dual-Direction Valid-ObjC Ban Rejection Corpus (`ban_corpus_tests`)
Verifies every file in `examples/banned/valid_objc_*.m` in both directions: first confirming it compiles `-Werror` clean under stock `clang-23` as idiomatic Objective-C, and second confirming it fails under `build/libVeriObjCPlugin.so` with the exact diagnostic phrase specified in `docs/veriobjc-ban-list.md`:

```bash
./tests/integration/run_ban_corpus_test.sh build/libVeriObjCPlugin.so .
```

---

## ⚡ Compiling Code with the VeriObjC Toolchain

### 0. Using the `veriobjc-cc` SDK Compiler Wrapper (Recommended)
Both `./build/bin/veriobjc-cc` (in-tree) and `<PREFIX>/bin/veriobjc-cc` (after `cmake --install build --prefix <PREFIX>`) automatically configure `-std=gnu23 -fblocks`, load `libVeriObjCPlugin.so` (`veriobjc-check` + `-fpass-plugin` IR lowering), add the SDK include paths (`rt`, `vstd`, `sys0`, `hw`, `host`), and link `libveriobjc_rt.a` when producing executables (without `-lobjc`):

```bash
# Compile & link a standalone VeriObjC executable in one command
./build/bin/veriobjc-cc -O2 -Werror examples/sealed_mixed_scope.m -o build/sealed_mixed_scope
./build/sealed_mixed_scope

# Include external C99 headers cleanly via -isystem
./build/bin/veriobjc-cc -O2 -Werror -isystem ./tests/plugin/isystem_ext \
                        -c examples/isystem_c99_frame_stage.m -o build/isystem_c99_frame_stage.o

# Run Z3 QF_BV contract verification directly through veriobjc-cc
./build/bin/veriobjc-cc --smt-verify --smt-report -Werror -fsyntax-only examples/vcontracts_bounds_proof.m
```

> **Note:** Passing `-fobjc-runtime=gnustep*` is explicitly rejected by `veriobjc-cc` because GNUstep ObjC runtime flags trigger an LLVM 23 ELF COMDAT backend crash and VeriObjC lowers all `@sealed` / `@final` dispatch statically without an Objective-C runtime.

### 1. Compiling with Raw `clang-23` + Clang Plugin Flags
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

### 3. Formal SMT Contract Verification with Z3 (`--smt-verify` & `--smt-report`)
Functions annotated with `vcontract_pre(...)` / `vcontract_post(...)` (`<vstd/contracts.h>`) or `veriobjc_pre(...)` / `veriobjc_post(...)` can be formally discharged at compile time over exact C two's-complement bit-vector semantics (`QF_BV`) via Z3:

| Plugin CLI Flag | Purpose |
| :--- | :--- |
| `--smt-verify` | Fail compilation closed on any violated `@post` (`Z3_SAT_COUNTEREXAMPLE`), timeout/inconclusive proof (`Z3_UNKNOWN_TIMEOUT`), or missing/outdated solver (`Z3_SOLVER_UNAVAILABLE`). |
| `--smt-report` | Print per-function Z3 discharge status (`Solver: Z3 version ... | Status: UNSAT_PROVEN \| SAT_COUNTEREXAMPLE \| UNKNOWN_TIMEOUT`). |
| `--smt-timeout-ms=<ms>` | Per-function Z3 timeout budget in milliseconds (default: `2000`; fails closed with `Z3_UNKNOWN_TIMEOUT` if exceeded). |
| `--z3-path=<path>` | Override the `z3` executable path (must report `Z3 version >= 4.8.12`; fails closed with `Z3_SOLVER_UNAVAILABLE` otherwise). |

**Proven contract (`UNSAT_PROVEN`):**
```bash
clang-23 -std=gnu23 -fblocks \
         -Xclang -load -Xclang build/libVeriObjCPlugin.so \
         -Xclang -add-plugin -Xclang veriobjc-check \
         -Xclang -plugin-arg-veriobjc-check -Xclang --smt-verify \
         -Xclang -plugin-arg-veriobjc-check -Xclang --smt-report \
         -Xclang -plugin-arg-veriobjc-check -Xclang --smt-timeout-ms=2000 \
         -I./rt/include -I./vstd/include \
         -fsyntax-only examples/vcontracts_bounds_proof.m
```
Output:
```text
[VeriObjC --smt-report] Function: clamp_limit_price | Pre: (assert (price >= 1 && price <= 100000)) | Post: (assert (not (result >= 1 && result <= 100000))) | Solver: Z3 version 4.8.12 - 64 bit | Status: UNSAT_PROVEN
[VeriObjC --smt-report] Function: round_up_dma_frame_64 | Pre: (assert (bytes <= 8192)) | Post: (assert (not ((result & 63) == 0 && result >= bytes && result <= 8192))) | Solver: Z3 version 4.8.12 - 64 bit | Status: UNSAT_PROVEN
```

**Violated contract (`Z3_SAT_COUNTEREXAMPLE` with concrete witness):**
```bash
clang-23 -std=gnu23 -fblocks \
         -Xclang -load -Xclang build/libVeriObjCPlugin.so \
         -Xclang -add-plugin -Xclang veriobjc-check \
         -Xclang -plugin-arg-veriobjc-check -Xclang --smt-verify \
         -I./rt/include -I./vstd/include \
         -fsyntax-only tests/plugin/negative/test_banned_smt_z3_counterexample.m
```
Output (32-bit signed overflow caught by `QF_BV` bit-vector reasoning):
```text
tests/plugin/negative/test_banned_smt_z3_counterexample.m:10:39: error: VeriObjC Violation: SMT contract counterexample (Z3_SAT_COUNTEREXAMPLE) in function 'unchecked_add_margin_i32' at 10:39: postcondition '@post(result > price)' violated under '@pre(price >= 0 && margin >= 1 && margin <= 100)' with witness { price = 2147483586, margin = 68, result = -2147483642 }.
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
clang-23 -O2 -ffreestanding -nostdlib -DVERIOBJC_ASM_PROBE -I./rt/include \
         -c rt/src/veriobjc_runtime.c -o build/veriobjc_runtime_probe.o
llvm-objdump-23 -d build/veriobjc_runtime_probe.o --disassemble-symbols=veriobjc_runtime_scope_guard_test
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
CPPUTEST_ROOT="${CPPUTEST_ROOT:-../cpputest}"
clang++-23 -O0 -fprofile-instr-generate -fcoverage-mapping -std=c++26 \
    -Irt/include -isystem "$CPPUTEST_ROOT/include" \
    tests/unit_rt/main.cpp tests/unit_rt/TestStackAlloc.cpp \
    tests/unit_rt/TestPoolAlloc.cpp tests/unit_rt/TestGenPool.cpp \
    tests/unit_rt/TestScopeAndLayout.cpp tests/unit_rt/TestDispatch.cpp \
    tests/unit_rt/TestTapeAndRelPtr.cpp \
    "$CPPUTEST_ROOT/cpputest_build/src/CppUTest/libCppUTest.a" \
    "$CPPUTEST_ROOT/cpputest_build/src/CppUTestExt/libCppUTestExt.a" \
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
| **Micro-Runtime Header & Archive** | `rt/include/veriobjc_runtime.h`, `build/libveriobjc_rt.a` | Freestanding data stacks, pools, object layouts, and static runtime archive |
| **SDK Compiler Driver** | `build/bin/veriobjc-cc` (`<PREFIX>/bin/veriobjc-cc`) | Drop-in compiler/linker wrapper injecting `veriobjc-check`, IR pass, headers, and `libveriobjc_rt.a` |
| **Clang Plugin** | `build/libVeriObjCPlugin.so` | Clang 23 AST & LLVM IR verification/lowering plugin (`veriobjc-check`) |
| **Host Staged I/O** | `host/include/veriobjc_host_io.h` | `@cold` bounded static-staging file loader (`veriobjc_host_load_file_staged`) |
| **Unit Test Binary** | `build/unit_rt_tests` | CppUTest runner for micro-runtime |
| **Plugin Test Runner** | `tests/plugin/run_plugin_tests.sh` | Automated positive/negative AST compiler test script |
| **Execution Plan** | `plan.md` | Living checklist of completed and upcoming tasks |
