# 🗺️ VeriObjC Master Implementation & TDD Execution Plan (`plan.md`)

This living checklist tracks the complete execution of **VeriObjC (Verifiable Objective-C)** across its compiler plugin, freestanding micro-runtime (`rt/`), standard library (`vstd/`), and verification harnesses.

Every implementation step follows strict **Test-Driven Development (TDD: Red ➔ Green ➔ Refactor ➔ Audit)**. Tasks are mapped directly to the specifications in [`docs/`](file:///home/user/veriObjC/docs/).

---

## 🧭 Progress Dashboard

* **Overall Status:** Active Development — Phase 1: Micro-Runtime Core
* **Compiler Substrate:** Clang / LLVM 23.1.5 (`/usr/bin/clang-23`)
* **Test Framework:** CppUTest (`/home/user/cpputest`)
* **Build System:** CMake + Make / Ninja
* **Version Control:** Git (`main` / `master`)

---

## 📦 Phase 0: Toolchain, Build System & Environment Setup

- [x] **Task 0.1: VCS Initialization & Structure**
  - Git repository initialized with appropriate `.gitignore`.
  - Directory structure created: `rt/`, `compiler/`, `vstd/`, `tests/` (`unit_rt/`, `plugin/`, `integration/`).
- [x] **Task 0.2: Unified CMake Configuration**
  - Configured C23 / C++20 with Clang 23.
  - Linked CppUTest static libraries and headers.
  - Test runner executable `unit_rt_tests` configured and running via CTest.

---

## ⚡ Phase 1: Freestanding Closed-World Micro-Runtime (`rt/`)
*(Mapped to: `docs/veriobjc-poc-execution-plan-updated.md` Module 2 & `docs/veriobjc-proof-of-concept-updated.md` Phase 1)*

### 1.1 Data-Stack Substrate (`veriobjc_stack`)
- [x] **Task 1.1.1: Stack Initialization & Capacity Invariants [TDD Cycle 1]**
  - Bounded initialization (`base`, `top`, `size`).
  - Zero-heap allocation; O(1) state tracking.
- [x] **Task 1.1.2: Contiguous Allocation & Word Alignment [TDD Cycle 1]**
  - `veriobjc_stack_alloc` advances top in O(1) with pointer alignment (`sizeof(void*)`).
  - Overflow and integer wrap-around protection.
- [x] **Task 1.1.3: Reference Stability Across Growth [TDD Cycle 1]**
  - Proven invariant: allocating new elements never reallocates or moves prior data.
- [x] **Task 1.1.4: One-Store Scope Reset (`@arena` / `@scoped`) [TDD Cycle 1]**
  - `veriobjc_stack_reset` restores top pointer to scope mark.
  - Disassembly audit: verified compiled to **exactly one `movq` store instruction**.
  - Binary audit (`nm`): verified **zero references to libc/malloc**.
- [x] **Task 1.1.5: 100% Branch Coverage & Edge-Case Hardening**
  - Audited via `llvm-cov` with all True/False branches covered (null pointers, boundary additions, small instance padding).
- [x] **Task 1.1.6: Closed-World Object Construction Hook (`veriobjc_alloc_init`)**
  - Lay down `isa` and object instance memory contiguously on the data stack.

### 1.2 Reusable Freelist Pools (`veriobjc_pool`)
*(Mapped to: `docs/veriobjc-foundational-library-architecture-updated.md` §3 & `docs/veriobjc-proof-of-concept-updated.md` Phase 1)*
- [x] **Task 1.2.1: Pool Initialization over Data Stack [TDD Cycle 2]**
  - Backing memory tied to a `veriobjc_stack`.
  - Slot size fixed statically; initial high-water mark count = 0; free_head = `UINT32_MAX`.
- [x] **Task 1.2.2: Pool Allocation (`veriobjc_pool_alloc`) [TDD Cycle 2]**
  - If freelist non-empty: pop freelist head in O(1).
  - If freelist empty: bump data stack by `slot_size` and increment high-water count.
  - Zero general heap calls.
- [x] **Task 1.2.3: Pool Free (`veriobjc_pool_free`) [TDD Cycle 2]**
  - Push slot index onto the embedded freelist head.
  - Memory itself is never released to an OS allocator.
- [x] **Task 1.2.4: Same-Typed Stale Read Invariant Test [TDD Cycle 2]**
  - Reading a freed pool slot yields a valid address of the correct type, never memory corruption.
  - High-water mark stable: 100 re-allocations resolved from freelist; data stack did not grow.
  - Binary audit: zero undefined symbols (`nm`), zero libc/malloc references.
- [ ] **Task 1.2.5: Weak Generation-Counted Handle (`vweak<T>`) [TDD Cycle 3]**
  - Pool slot header tracks generation counter bumped on `alloc` and `free`.
  - Stale `vweak` access detects generation mismatch, reports `stale` instead of silently reading garbage.

### 1.3 Headerless Object Packing & Link-Time Sealed Dispatch
*(Mapped to: `docs/veriobjc-poc-execution-plan-updated.md` Module 2, Tasks 1 & 3)*
- [ ] **Task 1.3.1: Zero-Base-Tax Object Layout Audit**
  - Headerless `@owned` object packing: `sizeof(Object)` matches exact sum of ivars with zero prefix metadata bytes.
- [ ] **Task 1.3.2: Link-Time Perfect-Hashed Jump Table Resolver**
  - 32-bit integer selector hash resolution.
  - Conflict-free dispatch table placed in `.rodata`.
- [ ] **Task 1.3.3: Scoped Cleanup Hook (`VeriObjC_ScopeRelease`)**
  - `__attribute__((cleanup))` automation resetting stack windows upon block termination.

### 1.4 Self-Relative Tape Format
*(Mapped to: `docs/veriobjc-poc-execution-plan-updated.md` Module 2, Task 5)*
- [ ] **Task 1.4.1: Self-Relative Link Primitives (`__relative(8|16|32)`)**
  - Narrow offset pointer encoding for position-independent structures.
- [ ] **Task 1.4.2: Verified Framing & Loading (`to_bytes` / `from_bytes`)**
  - Direct serialization without schemas; fuzz test corrupt bytes rejection.

---

## 🛠️ Phase 2: Clang Frontend Plugin (`compiler/`)
*(Mapped to: `docs/veriobjc-poc-execution-plan-updated.md` Module 1, Tasks 1–16)*

### 2.1 AST Plugin Boilerplate & Registry
- [ ] **Task 2.1.1: Clang 23 `PluginASTAction` Infrastructure**
  - CMake target for `libVeriObjCPlugin.so` linked against `libclang-cpp23` and LLVM 23.
  - Registration under `-plugin veriobjc-check`.
  - Smoke test: Clang loads plugin without binary or ABI mismatch.

### 2.2 Strict Zero-Bypass Dynamic Feature Ban
- [ ] **Task 2.2.1: Ban Generic `id` Type [Negative AST Tests]**
  - Matcher for `VarDecl`, `ParmVarDecl`, `FieldDecl` with `isObjCIdType()`.
  - Immediate fatal error and compilation halt.
- [ ] **Task 2.2.2: Ban `NSObject` Inheritance & Apple Runtime Reflection**
  - Immediate rejection of dynamic class queries, `NSClassFromString`, swizzling hooks.
- [ ] **Task 2.2.3: Untyped Receiver Ban on `ObjCMessageExpr`**
  - Demand nominal interface types on all bracketed message calls.

### 2.3 Nominal Devirtualization & Sealed Dispatch
- [ ] **Task 2.3.1: Devirtualization of `@final` / Known Types**
  - Rewrite `[receiver message]` directly into direct C-function calls (`CallExpr`).
- [ ] **Task 2.3.2: Sealed Protocol Dispatch Matrix**
  - Compile-time check for `__attribute__((objc_sealed))`.
  - Lower polymorphic calls to static jump tables.

### 2.4 Linear Ownership & Data-Stack State Tracker
- [ ] **Task 2.4.1: Move Semantics & Consumed State Tracker**
  - Linear ownership graph: `Unallocated -> Active -> Consumed/Dead`.
  - Double-free and use-after-move compile rejection.
- [ ] **Task 2.4.2: Reference Root Safety**
  - Statically prove every data-stack reference is bounded by its declaring scope mark.
  - Rejection of escaping data-stack references.
- [ ] **Task 2.4.3: Flat-Value Channel Enforcement**
  - Statically verify payload types of `vatomic_channel` contain no pointer references.

### 2.5 Syntactic Lowering Passes
- [ ] **Task 2.5.1: Range-Based For Loop Lowering**
  - Lower `for (auto &x : container)` into raw pointer increments (`ForStmt`).
- [ ] **Task 2.5.2: Multi-Frame Error Return Lowering (`return err from f`)**
  - Statically check innermost active frame of `f` and lower error flag without stack unwinder.
- [ ] **Task 2.5.3: Non-Escaping Function Blocks**
  - Monomorphize function blocks; compile-time rejection of heap-escaping blocks.
- [ ] **Task 2.5.4: Constrained Generics (`vgeneric`) Definition-Site Checking**
  - Check constraints once at definition site; prevent template explosion.
- [ ] **Task 2.5.5: Fueled Interpreter for `vconstexpr`**
  - Fuel counter (max steps / recursion depth); clean diagnostics on exhaustion.
- [ ] **Task 2.5.6: Compile-Time Regex (`vregex`) to DFA in `.rodata`**
  - Subset construction and Hopcroft minimization into static lookup table.

---

## 📚 Phase 3: Foundational Standard Library (`vstd/`)
*(Mapped to: `docs/veriobjc-foundational-library-architecture-updated.md`)*

- [ ] **Task 3.1: Text & Slices (`<vstd/string.h>`, `<vstd/span.h>`)**
  - Non-allocating `vstring_view` and contiguous `vspan<T>`.
- [ ] **Task 3.2: Stack Arrays (`<vstd/stack.h>`)**
  - `vstack_array<T>` integrated with compiler-assigned data stacks.
- [ ] **Task 3.3: Inplace Containers (`<vstd/vector.h>`)**
  - Fixed capacity `vvector<T, N>` with compile-time bounded assertions.
- [ ] **Task 3.4: Cache-Aligned Maps (`<vstd/map.h>`, `<vstd/hash.h>`)**
  - `vflat_map` (parallel contiguous binary search) and `vhash_flat` (open addressing).
- [ ] **Task 3.5: Algebraic Data Types (`<vstd/variant.h>`)**
  - `vvariant` with exhaustive compile-time checked `match`.
- [ ] **Task 3.6: Strong Types (`<vstd/strong.h>`)**
  - `vstrong` newtype wrappers with zero implicit conversions.
- [ ] **Task 3.7: Branch-Proven Result (`<vstd/result.h>`)**
  - `vresult<T, E>` variant unions with required branch checking before access.
- [ ] **Task 3.8: Concurrency & Linear Guards (`<vstd/channel.h>`, `<vstd/atomic.h>`)**
  - Lock-free SPSC flat-value channels and linear `vatomic<T>`.

---

## 📊 Phase 4: Measurement, Latency CI & Verification ("No Number, No Claim")
*(Mapped to: `docs/veriobjc-poc-execution-plan-updated.md` Module 3)*

- [ ] **Task 4.1: Cycle-Accurate Microbenchmark Harness**
  - CPU cycle measurement (`rdtsc` / `CNTVCT_EL0`) on isolated cores.
- [ ] **Task 4.2: Survivor Reports as Build Artifacts**
  - `--effects-report`: per-line survivor report of effectful call sites.
  - `--branch-report`: classification of unproven data-dependent branches.
  - `--mono-report`: monomorphization byte budget and instantiation tracking.
- [ ] **Task 4.3: Latency Budget Gates**
  - CI budget check: p50/p99 regression beyond noise band fails build.
- [ ] **Task 4.4: Formal Math Proof Integration (SMACK / Z3)**
  - Extraction of `@pre` and `@post` boundary equations into SMT solver assertions.

---

## 🔄 Phase 5: End-to-End Integration Verification

- [ ] **Task 5.1: Network Processor Kernels & Audio DSP Examples**
  - Compile [`veriobjc-examples-updated.md`](file:///home/user/veriObjC/docs/veriobjc-examples-updated.md) files through toolchain.
  - Verify disassembly contains **0 calls to `objc_msgSend`**.
- [ ] **Task 5.2: Round-Trip Snapshot Persistence**
  - Replay order book snapshot through `to_bytes` / `from_bytes` with fuzz corruption check.
