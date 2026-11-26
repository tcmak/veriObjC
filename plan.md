# 🗺️ VeriObjC Master Implementation & TDD Execution Plan (`plan.md`)

This living checklist tracks the complete execution of **VeriObjC (Verifiable Objective-C)** across its modular compiler plugin (`compiler/`), freestanding micro-runtime (`rt/`), standard library (`vstd/`), and host verification harnesses (`host/`).

Every implementation step follows strict **Test-Driven Development (TDD: Red ➔ Green ➔ Refactor ➔ Audit)** governed by [`AGENTS.md`](./AGENTS.md) and the specifications in [`docs/`](./docs/).

---

## 🧭 Progress Dashboard

* **Overall Status:** Phases 0–5 & Block 6.0 Complete (`veriobjc-cc` SDK driver, `@cold` static staging file loader, `-isystem` C99 interop, mixed-size `@sealed` scope composition & real Z3 `QF_BV` SMT contract discharge); **Next Up:** Task 6.0 (`vosh` — Self-Hosted Zero-Boilerplate Scripting & Verification Runner)
* **Compiler Substrate:** Clang / LLVM 23.1.5 (`clang-23`) + Z3 ≥ 4.8.12 (64-bit host prover), split cleanly across `PluginMain.cpp`, `AstBanChecks.cpp`, `CfgDataflowChecks.cpp`, `ContainerChecks.cpp`, `StaticDispatchLowering.cpp`, and `SmtContractVerifier.cpp`
* **Test Framework:** CppUTest (`CPPUTEST_ROOT`)
* **Build System:** CMake + Make / Ninja (portable `find_package(LLVM REQUIRED CONFIG)`)
* **Version Control:** Git (`main` / `master`, standalone repository with zero external submodules)
* **CLI & Build Guide:** See [`BUILDING.md`](./BUILDING.md) for command line instructions.
* **Planned (Draft):** Phase 7 — 7a Low-Latency Library Kit (zero compiler changes), 7b Reference Demonstration (dogfooding the kit; two-pass v1/v2 measurement around Phase 8), 7c Language-Property Hardening (gated on 7b evidence), 7d Dialect Spec & Stability Governance (order-independent; Hare-inspired), 7e Computational-Finance Lessons Applied (blocklist allocator, false-sharing discipline, checkpointed replay, cold-side batch executor, parallel determinism; rides 7a/7b). **Phase 8 (Draft) — SMT-Backed Refinement & Index Types (`QF_BV`):** middle-ground dependent-type ergonomics, registered under Phase 7d gates (appended at the end of this file; wording pending a planning pass)

---

## 📦 Phase 0: Toolchain, Build System & Environment Setup

- [x] **Task 0.1: VCS Initialization & Structure**
  - Git repository initialized with `.gitignore`, `.gitattributes` (LF), `LICENSE`, and `AGENTS.md`.
  - Directory structure: `rt/`, `compiler/`, `vstd/`, `sys0/`, `hw/`, `host/`, `tests/` (`unit_rt/`, `plugin/`, `integration/`).
- [x] **Task 0.2: Unified CMake Configuration**
  - Configured C23 / C++26 with Clang 23 via portable `LLVM_DIR` / `CPPUTEST_ROOT` discovery.
  - Test runner executable `unit_rt_tests`, `plugin_tests`, and integration gates running in CTest and `.github/workflows/ci.yml`.

---

## ⚡ Phase 1: Freestanding Closed-World Micro-Runtime (`rt/`)
*(Mapped to: `docs/veriobjc-poc-execution-plan.md` Module 2 & `docs/veriobjc-proof-of-concept.md` Phase 1)*

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
*(Mapped to: `docs/veriobjc-foundational-library.md` §3 & `docs/veriobjc-proof-of-concept.md` Phase 1)*
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
- [x] **Task 1.2.5: Weak Generation-Counted Handle (`vweak<T>`) [TDD Cycle 3]**
  - Implemented `veriobjc_gen_pool` and `veriobjc_handle` (`slot`, `generation`).
  - Active slots hold odd generation; freeing bumps to even generation; reallocating bumps to next odd.
  - Stale handle access provably detects generation mismatch and returns `NULL` (`stale`) instead of reading garbage.
  - Double-free is mathematically rejected via generation state check.
  - 100% branch coverage verified with `llvm-cov`; zero libc/malloc symbols verified with `nm`.

### 1.3 Headerless Object Packing & Link-Time Sealed Dispatch
*(Mapped to: `docs/veriobjc-poc-execution-plan.md` Module 2, Tasks 1 & 3)*
- [x] **Task 1.3.1: Zero-Base-Tax Object Layout Audit [TDD Cycle 4]**
  - Headerless `@owned` object packing: `sizeof(Object)` matches exact sum of ivars with zero prefix metadata bytes.
  - Polymorphic objects carry strictly `sizeof(void*)` / 8-byte sealed header.
- [x] **Task 1.3.2: Freestanding Dispatch Table Primitives (`<rt/dispatch.h>`) [TDD Cycle 7]**
  - 32-bit FNV-1a integer selector hash resolution (`veriobjc_sel_hash`), conflict-free `.rodata` dispatch table (`veriobjc_vtable`, `veriobjc_vtable_entry`) with O(1) `(sel_id >> shift) & mask` lookup, and 8-byte `veriobjc_sealed_header` (`veriobjc_alloc_sealed`) supporting compiler-synthesized closed-world tag dispatch.
- [x] **Task 1.3.3: Scoped Cleanup Hook (`VeriObjC_ScopeRelease` / `VERIOBJC_SCOPED_STACK`) [TDD Cycle 4]**
  - RAII-style `__attribute__((cleanup))` automation resetting stack windows upon block termination.
  - Nested scopes unwind deterministically in LIFO order; disassembles to a single `movq` store instruction.
  - 100% branch and null guard coverage; zero libc symbols in `nm`.

### 1.4 Self-Relative `VOBJ` v1 Snapshot Format
*(Mapped to: `docs/veriobjc-poc-execution-plan.md` Module 2, Task 5)*
- [x] **Task 1.4.1: Self-Relative Link Primitives (`__relative(8|16|32)`) [TDD Cycle 8]**
  - Narrow offset pointer encoding (`veriobjc_rel8`, `veriobjc_rel16`, `veriobjc_rel32`) for position-independent structures across raw `memcpy`.
  - Range overflow checks, self-link sentinel rejection (`delta == 0`), and null safety.
- [x] **Task 1.4.2: Verified Framing & Loading (`to_bytes` / `from_bytes`) [TDD Cycle 9]**
  - Framed snapshot serialization (`veriobjc_tape_header`, `veriobjc_to_bytes`) and zero-copy verified loading (`veriobjc_from_bytes`) with 32-bit FNV-1a checksum and structural invariants.
  - Exhaustive byte-by-byte mutation fuzz sweep and hostile header rejection verified in `TestTapeAndRelPtr.cpp`.
- [x] **Task 1.4.3: Self-Relative Link Bounds & Alignment Verification (`veriobjc_verify_rel32_link`) [TDD Cycle 9]**
  - Bounds-checked and element-alignment-checked verification of loaded `__relative(32)` links before dereference.
  - 100.00% region, function, and branch coverage verified via `llvm-cov-23`; zero external/libc symbols verified via `nm`.

---

## 🛠️ Phase 2: Modular Clang Frontend & LLVM IR Plugin (`compiler/`)
*(Mapped to: `docs/veriobjc-poc-execution-plan.md` Module 1, Tasks 1–16)*

### 2.1 Modular Plugin Infrastructure
- [x] **Task 2.1.1: Clang 23 `PluginASTAction` & Modular Split [TDD Cycle 5 / Task C2.1a]**
  - CMake target `libVeriObjCPlugin.so` split cleanly across `PluginMain.cpp`, `AstBanChecks.cpp`, `CfgDataflowChecks.cpp`, `ContainerChecks.cpp`, and `StaticDispatchLowering.cpp` (`compiler/include/veriobjc/VeriObjCVisitor.h`).
  - Registered under `-plugin veriobjc-check` / `-add-plugin veriobjc-check` and `-fpass-plugin=`.

### 2.2 Strict Zero-Bypass Dynamic Feature Ban (`AstBanChecks.cpp`)
- [x] **Task 2.2.1: Ban Generic `id` Type [Negative AST Tests] [TDD Cycle 5]**
  - Intercepts `VarDecl`, `ParmVarDecl`, `FieldDecl`, and `TypedefNameDecl` using `isObjCIdType()`.
  - Verified across negative test suite (`test_banned_id.m`, `test_banned_id_field.m`, `test_banned_id_param.m`) and positive tests.
- [x] **Task 2.2.2: Ban `NSObject` Inheritance & Apple Runtime Reflection [TDD Cycle 6]**
  - Rejects any inheritance from `NSObject` and calls to dynamic reflection/swizzling functions (`NSClassFromString`, `class_copyMethodList`, `objc_getClass`, `objc_msgSend`, `method_exchangeImplementations`).
- [x] **Task 2.2.3: Untyped Receiver Ban on `ObjCMessageExpr` [TDD Cycle 6]**
  - Validates `E->getReceiverInterface() != nullptr` (`test_banned_nsobject.m`, `test_banned_reflection.m`, `test_banned_untyped_msg.m`).

### 2.3 Nominal Devirtualization & Sealed Dispatch (`StaticDispatchLowering.cpp`)
- [x] **Task 2.3.1: Devirtualization of `@final` / Known Types [TDD Cycle 10 / Task C2.3]**
  - Closed-world selector check in `VisitObjCMessageExpr`: rejects undeclared selectors (`test_banned_unresolved_msg.m`) without mutating `ObjCMessageExpr` in the AST.
  - LLVM IR `PassInfoMixin` lowering pass (`VeriObjCStaticDispatchPass` in `compiler/src/StaticDispatchLowering.cpp`) rewrites monomorphic/`@final` `@objc_msg_lookup` call sites directly to `@_i_<Class>__<Selector>` (or `@__veriobjc_direct_*`) and strips `@.objc_load_function`, `@llvm.global_ctors`, `@__objc_exec_class`, and `.objc_*`/`_OBJC_*` metadata globals (`0` ObjC runtime symbols verified via `nm` and `llvm-objdump -d`).
- [x] **Task 2.3.2: Sealed Class & Protocol Dispatch Matrix [TDD Cycle 11 / Task C2.3]**
  - First-class `__attribute__((objc_sealed))` and `__attribute__((objc_final))` Clang Sema attributes enforcing sealed class inheritance (`test_banned_unsealed_inheritance.m`, `valid_objc_unsealed_inheritance.m`), final class non-subclassability (`test_banned_final_inheritance.m`), sealed protocol conformance (`test_banned_unsealed_protocol.m`), and 16-bit closed-world tag uniqueness.
  - Freestanding 8-byte prefix header (`struct veriobjc_sealed_header`, `veriobjc_alloc_sealed` in `<rt/dispatch.h>`, policed by `CfgDataflowChecks.cpp` via `test_banned_escape_sealed_upcast.m`) + LLVM IR tag-switched dispatch synthesis (`@__veriobjc_dispatch_<Class>__<Selector>`) proven end-to-end without `-lobjc` in `tests/integration/test_e2e_sealed_polymorphism.m` and `examples/sealed_synth_hierarchy.m`.

### 2.4 Linear Ownership & Data-Stack State Tracker (`CfgDataflowChecks.cpp`)
- [x] **Task 2.4.1: Move Semantics & Consumed State Tracker [TDD Cycle 12 / Task C2.2]**
  - Path-sensitive forward monotone dataflow worklist over `clang::CFG` (`compiler/src/CfgDataflowChecks.cpp`) tracking `Unreached -> Live/Moved -> MaybeMoved` across `if`/`else` join blocks, local pointer aliases, and loop back-edges via `veriobjc_move()` / `__veriobjc_mark_moved` and `__attribute__((objc_consumed))`.
  - Double-free, alias use-after-move, and loop back-edge use-after-move rejected at compile time while branch-symmetric moves are accepted (`test_banned_double_free.m`, `test_banned_use_after_move.m`, `test_banned_use_after_move_alias.m`, `test_banned_use_after_move_loop_backedge.m`, `test_valid_branch_symmetric_move.m`).
- [x] **Task 2.4.2: Reference Root Safety [TDD Cycle 12 / Task C2.2]**
  - `clang::CFG` stack-scope provenance lattice (`StackScoped(depth)`) + interprocedural pointer-forwarding summaries proving data-stack allocations and views (`vstring_view`, `vspan`) never escape via outer variables, globals, struct fields, out-parameters, subobject addresses (`&inner[0]`), return values, or identity/forwarding helper functions (`test_banned_escaping_stack_ref.m`, `test_banned_escape_*.m`, `valid_objc_escape_via_out_param.m`).
- [x] **Task 2.4.3: Flat-Value Channel Enforcement [TDD Cycle 12]**
  - Registered `__attribute__((objc_flat))` Sema attribute and statically verifies payload types of `veriobjc_channel_send` and `objc_flat` structs/typedefs contain zero pointer/reference fields at any nesting depth (`test_banned_nonflat_channel.m`, `test_valid_linear_and_channel.m`).

### 2.5 Syntactic & Compile-Time Passes (`AstBanChecks.cpp` / `StaticDispatchLowering.cpp`)
- [x] **Task 2.5.1: Range-Based For Loop Check & Lowering [TDD Cycle 13]**
  - Lowering and bounded-container enforcement for `ObjCForCollectionStmt` (`for (Elem *x in container)`), eliminating `objc_enumerationMutation` and rejecting unbounded containers (`test_valid_range_for.m`, `test_banned_unbounded_for_in.m`).
- [x] **Task 2.5.2: Multi-Frame Error Return Verification (`return err from f`) [TDD Cycle 14]**
  - Static call-graph active-frame verifier ensuring every `veriobjc_return_err_from(err, target_frame)` call site provably terminates inside an active `target_frame` without an exception unwinder (`test_valid_err_from.m`, `test_banned_uncaught_err_from.m`).
- [x] **Task 2.5.3: Non-Escaping Function Blocks [TDD Cycle 15]**
  - Monomorphizes non-escaping `BlockExpr` closures into direct static C functions while rejecting escaping blocks (`test_valid_function_block.m`, `test_banned_escaping_block.m`).
- [x] **Task 2.5.4: Constrained Generics (`vgeneric`) Definition-Site Checking [TDD Cycle 16]**
  - Enforces explicit sealed constraint bounds (`<T : SealedBound *>`) on generic type parameters and rejects unconstrained or unsealed bounds (`test_valid_constrained_generic.m`, `test_banned_unconstrained_generic.m`).
- [x] **Task 2.5.5: Fueled Interpreter for `vconstexpr` [TDD Cycle 17]**
  - AST-level fuel-bounded interpreter (`MAX_STEPS = 4096`, `MAX_DEPTH = 64`) with memoization and deterministic fuel-exhaustion diagnostics (`test_valid_vconstexpr.m`, `test_banned_vconstexpr_fuel.m`).
- [x] **Task 2.5.6: Compile-Time Regex (`vregex`) to Minimized DFA in `.rodata` [TDD Cycle 18]**
  - Thompson NFA → Subset Construction → Hopcroft Minimization inside `compiler/src/` lowering `vregex_match` into `.rodata` transition tables and rejecting backreferences/lookarounds/state explosion (`test_valid_vregex.m`, `test_banned_backref_regex.m`, `test_regex_state_explosion.m`).
- [x] **Task 2.5.7: Nominal Local Type Inference (`auto`) [TDD Cycle 19]**
  - Exact nominal local deduction rejecting array decay and `auto` on parameters/exported returns (`test_valid_auto_local.m`, `test_banned_auto_param.m`, `test_banned_auto_export.m`).

### 2.6 Fixed-Point Streaming Kernel Checks (`<vstd/stream.h>`)
*(Mapped to: `docs/veriobjc-advanced-substrate.md` §6 — Language-side contracts only; RTL synthesis belongs outside `veriObjC`)*
- [x] **Task 2.6.1: Wire-Level Stream Types (`Bits<W>` / `Stream<T, N>`) [TDD Cycle 20]**
  - Compile-time width and context-cap verification (`test_banned_stream_width_mismatch.m`, `test_banned_context_overflow.m`).
- [x] **Task 2.6.2: Branchless CMUX & Popcount Reduction Primitives [TDD Cycle 20]**
  - Pure-CPU branchless `veriobjc_cmux`, `veriobjc_ternary_popcount_reduce`, and `veriobjc_ternary_dot_512` with `vstream_latency_contract` (`test_valid_ternary_dot.m`, `examples/ternary_glu_block.m`).
- [x] **Task 2.6.3: `objc_hw_kernel` Zero-Multiplication & Transitive Call-Graph Gate [TDD Cycle 20 / Task C2.2]**
  - Fails compilation on both direct and transitive helper runtime multiplication/division/modulo (`*`, `*=`, `/`, `/=`, `%`, `%=`) across the call graph (`compiler/src/CfgDataflowChecks.cpp`, `test_banned_zero_dsp_helper_mul.m`).

### 2.7 Ban List Compliance Suite
- [x] **Task 2.7.1: Centralized Ban Registry & Diagnostic Compliance Tests [TDD Cycle 21]**
  - Single-source ban registry (`docs/veriobjc-ban-list.md`) mapped 1:1 to `tests/plugin/negative/test_*.m`.
- [x] **Task 2.7.2: Valid-ObjC Rejection Corpus (`examples/banned/`) [TDD Cycle 21]**
  - Dual-direction gate (`tests/integration/run_ban_corpus_test.sh`, `ban_corpus_tests`): compiles `-Werror` clean under stock Clang 23 and fails under `veriobjc-check` with the exact named diagnostic.

### 2.8 Effect System, FFI Discipline, Reflection & Topology
- [x] **Task 2.8.1: Effect Type System & `@nolatency` Enforcement [TDD Cycle 22]**
  - Effects (`@alloc`, `@io`, `@blocking`, `@fence`, `@shared-retain`), `@nolatency`/`@pure`/`@cold` quarantine checks, and `--effects-report`.
- [x] **Task 2.8.2: FFI Effect & Boundary Checking [TDD Cycle 22]**
  - Default `@cold` on unannotated `extern fn`, flat/handle-only `@export` ABI enforcement, allocator-mixing ban, and `--export-header-stdout`.
- [x] **Task 2.8.3: Compile-Time Reflection & SoA Transmuter [TDD Cycle 23]**
  - `veriobjc_reflect_*` and `vsoa_def3` lowered to 64-byte-aligned C arrays (`test_valid_reflect_soa.m`).
- [x] **Task 2.8.4: `vvariant`/`vmatch` Exhaustiveness & `vtrait` Closed Tables [TDD Cycle 23]**
  - Exhaustive `vmatch` verification (`test_banned_nonexhaustive_match.m`, `test_valid_vvariant_match.m`) and `objc_vtrait`.
- [x] **Task 2.8.5: Monomorphization Deduplication & Instantiation Budgets [TDD Cycle 23]**
  - Signature/body deduplication, budget enforcement (`test_banned_mono_budget.m`), and `--mono-report`.
- [x] **Task 2.8.6: Static Topology & Fixed Task DAGs [TDD Cycle 24]**
  - Cycle-checked static task DAGs (`test_banned_dag_cycle.m`), WCET constant folding, and IRQ-woken stack `vasync` frames (`test_valid_static_dag_vasync.m`).
- [x] **Task 2.8.7: Built-in-Place Construction Verification [TDD Cycle 24]**
  - `veriobjc_push_inplace` and `veriobjc_inplace_only` copy rejection (`test_banned_temp_copy_inplace.m`, `test_valid_inplace_construct.m`).

---

## 📚 Phase 3: Foundational Standard Library (`vstd/`)
*(Mapped to: `docs/veriobjc-foundational-library.md`)*

- [x] **Task 3.1: Text & Slices (`<vstd/string.h>`, `<vstd/span.h>`)**
- [x] **Task 3.2: Stack Arrays (`<vstd/stack.h>`)**
- [x] **Task 3.3: AST-Checked Inplace Containers (`<vstd/vector.h>` / `ContainerChecks.cpp`)**
  - Native C11/C23/ObjC `VVector_decl` / `VVector(T, N)` and constrained `VVector<T : VVectorItem *>` containers verified via Clang AST `ConstantArrayType` inspection (zero source-text preprocessing hacks; header-safe without `@end`; arbitrary capacities `N` by value; non-`@flat @final @sealed` by-value carriers rejected).
- [x] **Task 3.4: Cache-Aligned Maps (`<vstd/map.h>`, `<vstd/hash.h>`)**
- [x] **Task 3.5: Algebraic Data Types (`<vstd/variant.h>`)**
- [x] **Task 3.6: Strong Types (`<vstd/strong.h>`)**
- [x] **Task 3.7: Branch-Proven Result (`<vstd/result.h>`)**
- [x] **Task 3.8: Concurrency & Linear Guards (`<vstd/channel.h>`, `<vstd/atomic.h>`)**
- [x] **Task 3.9: Fixed-Point Streaming & Pure-CPU Ternary Primitives (`<vstd/stream.h>`, `<vstd/ternary.h>`)**
  - Pure-CPU round-half-up `veriobjc_ternary_dot_512` / `vternary_rmsnorm_shift`, token shift, GLU mux gate, and static-capacity KV arena checks.
- [x] **Task 3.10: Freestanding Floor (`sys0/`)**
- [x] **Task 3.11: Device & Real-Time Non-Blocking I/O (`<vstd/io.h>`, `hw/`)**
- [x] **Task 3.12: Lazy Execution Pipelines & `vasync` (`<vstd/execution.h>`)**
- [x] **Task 3.13: Hardware-Aware SIMD Vectorization (`<vstd/simd.h>`)**
- [x] **Task 3.14: Contracts Library (`<vstd/contracts.h>`)**
- [x] **Task 3.15: Cohesion & Minimal-Binary Gate (`tests/integration/check_vstd_cohesion.sh`)**

---

## 📊 Phase 4: Measurement, Latency CI & Verification ("No Number, No Claim")
*(Host-only harnesses isolated under `host/include/host/` so `rt/` stays strictly `-ffreestanding -mno-sse`)*

- [x] **Task 4.1: Cycle-Accurate Microbenchmark Harness (`<host/bench_harness.h>`)**
  - `rdtsc` / `CNTVCT_EL0` cycle extraction (`rt_bench_read_cycles`, `rt_bench_compute_stats`) with deterministic p50/p99/p99.9 + teardown stats (`tests/unit_rt/TestBenchHarness.cpp`). *(Note: keep general container/systems benchmarks domain-neutral without requiring ML-specific metadata.)*
- [x] **Task 4.2: Survivor Reports as Build Artifacts (`compiler/src/` & `<host/survivor_report.h>`)**
  - `--effects-report`, `--branch-report`, `--mono-report`, and diff audit engine (`rt_survivor_diff_audit`, `tests/unit_rt/TestSurvivorReport.cpp`).
- [x] **Task 4.3: Latency Budget Gates (`<host/latency_gate.h>`)**
  - Fail-closed p50/p99/p99.9/teardown regression gate (`rt_latency_gate_check`, `tests/unit_rt/TestLatencyBudgetGate.cpp`).
- [x] **Task 4.4: External SMT Solver Discharge (`Z3` CLI / Bit-Vector & LIA Contract Verification)**
  - Enforced `Z3 >= 4.8.12` (64-bit) discovery in `CMakeLists.txt` (`-DZ3_EXECUTABLE=...`, `-DVERIOBJC_REQUIRE_Z3=OFF`), `scripts/install.sh`, and `.github/workflows/ci.yml` while keeping `rt/` strictly freestanding (`-ffreestanding -mno-sse`, zero process/libc dependencies).
  - Implemented safe POSIX `pipe` + `fork` + `execvp` `z3 -smt2 -in` execution and cross-version witness parsing (`Z3 4.8.12` `(model ...)` through `Z3 4.13+` and `(get-value ...)`) in `<host/smt_bridge.h>` (`rt_z3_discharge_smtlib2`) plus exact C bit-vector (`QF_BV`) contract verification in `compiler/src/SmtContractVerifier.cpp` (`--smt-verify`, `--smt-report`, `--smt-timeout-ms=<ms>`, `--z3-path=<path>`).
  - Verified all four solver outcomes (`Z3_UNSAT_PROVEN`, Goose-style `Z3_SAT_COUNTEREXAMPLE` naming function, `line:col`, `@post`, and concrete witness bindings, fail-closed `Z3_UNKNOWN_TIMEOUT`, and `Z3_SOLVER_UNAVAILABLE`) across `tests/unit_rt/TestSmtContractBridge.cpp`, `tests/plugin/positive/test_valid_smt_z3_proof.m`, `tests/plugin/negative/test_banned_smt_z3_counterexample.m`, `examples/vcontracts_bounds_proof.m`, and `examples/banned/valid_objc_contract_counterexample.m` (`rt_contract_eval_endpoints_stub` retained strictly as a clearly labeled non-SMT unit helper).

---

## 🔄 Phase 5: End-to-End Integration Verification

- [x] **Task 5.1: Network Processor Kernels & Audio DSP Examples Smoke Suite (`run_examples_smoke_test.sh`)**
- [x] **Task 5.2: Round-Trip `VOBJ` v1 Snapshot Persistence (`order_book_snapshot.c`, `TestTapeAndRelPtr.cpp`)**
- [x] **Task 5.3: Ternary Block Elaboration & Transitive Zero-Multiplication Gate (`ternary_glu_block.m`, `cfg_branch_move_router.m`)**
- [X] **Task 5.4: retired/migrated to project setun and TernaryTape.jl
- [x] **Task 5.5: Mixed-Size Sealed Hierarchy Composition Test (Heterogeneous Arena Allocation + Upcast Tag Dispatch + One-Store Scope Reset)**
  - *Goal:* Prove sealed polymorphism, heterogeneous ivar sizes, stack-root escape policing, and one-store scope reset together in one end-to-end freestanding composition test.
  - *Deliverables (Red ➔ Green):*
    - Added `tests/integration/test_e2e_sealed_mixed_scope.m` (wired into `run_e2e_sealed_polymorphism_test.sh` / CTest `e2e_sealed_polymorphism_tests`) plus standalone `examples/sealed_mixed_scope.m` (wired into `run_examples_smoke_test.sh`), illegal upcast-escape corpus `examples/banned/valid_objc_sealed_upcast_escape.m` + `tests/plugin/negative/test_banned_escape_sealed_{span,compound_span,field_and_outparam}.m`, and updated `examples/README.md`.
    - Allocated concrete `@sealed @final` subclasses with differing ivar counts and byte sizes (`8B`, `16B`, `32B`) interleaved inside a single `VERIOBJC_SCOPED_STACK` arena on one `veriobjc_stack`, stored them in a `<vstd/span.h>` slice of superclass-typed pointers, and dispatched overridden selectors across the heterogeneous batch.
  - *Acceptance Criteria (Audit Trifecta):*
    - **Tag-switched dispatch & zero slicing:** Every superclass-pointer send resolves via `@__veriobjc_dispatch_<Class>__<Selector>` with all subclass ivars intact across differing object sizes; `nm` shows **zero** `objc_msgSend`, `objc_msg_lookup`, `__objc_exec_class`, `malloc`, or `free` symbols.
    - **One-store reset under heterogeneity:** Disassembly audit (`llvm-objdump -d` in `run_e2e_sealed_polymorphism_test.sh`) verifies scope exit restores `stack->top` to the scope mark via a single `movq` pointer store despite mixed concrete allocation sizes.
    - **Escape rejection:** Storing any upcast superclass pointer (directly or via `vspan_init` / compound-literal `vspan`) into an outer-scope variable, struct field, or out-parameter fails compilation under `CfgDataflowChecks.cpp` with a Goose-style root-escape diagnostic.
    - **Coverage:** Maintained 100.00% `llvm-cov` region/function/branch coverage on `rt/`.
- [x] **Task 5.6b: `VOBJ` v1 Single-Table Snapshot Persistence (`rt/`, `fixtures/vobj_v1/`)**
- [x] **Task 5.7: Host Tooling Isolation & Pure-CPU Round-Half-Up Ternary Fallback (`host/`, `<vstd/ternary.h>`)**

---

## 🧰 Phase 6: SDK Driver (veriobjc-cc), Scripting (vosh) & Package Manager (vpm)
*(Mapped to: `docs/veriobjc-poc-execution-plan.md` §0)*

### Block 6.0 — SDK Packaging, `veriobjc-cc` Driver, C99 Interop & Soundness Hardening
Real-world external consumer dogfooding and soundness audits surfaced ten usability, CI determinism, and verification sharp edges closed before `vosh` builds on top of the SDK:

- [x] **6.0.1 `cmake --install` SDK Layout & `veriobjc-cc` Compiler Wrapper (`CMakeLists.txt`, `tools/veriobjc-cc.in`, `scripts/install.sh`, `BUILDING.md`):**
  Installed a self-contained, relocatable prefix (`bin/veriobjc-cc`, `lib/veriobjc/libVeriObjCPlugin.so`, `lib/veriobjc/libveriobjc_rt.a`, `include/veriobjc/{rt,vstd,sys0,hw,host}`) alongside build-tree `build/bin/veriobjc-cc`. `veriobjc-cc` injects `-std=gnu23 -fblocks`, `-Xclang -load/-add-plugin veriobjc-check`, `-fpass-plugin` (omitted in `-E`/`-fsyntax-only` modes for `-Werror` cleanliness), and `-I` SDK headers (preserving user-site macro diagnostics), links `libveriobjc_rt.a` automatically when producing executables, forwards `--smt-*`/`--effects-report`/`--branch-report`/`--mono-report`/`--export-header-stdout`, and rejects `-fobjc-runtime=gnustep*` with an actionable error (`veriobjc_cc_sdk_tests`).
- [x] **6.0.2 Dangling COMDAT Guard in `StaticDispatchLowering` (`compiler/src/StaticDispatchLowering.cpp`):**
  Cleared `GO.setComdat(nullptr)` (and privatized `linkonce` `.objc_*` selector symbols) across `M.global_objects()` and skipped any referenced COMDAT before erasing `.objc_*` entries from `M.getComdatSymbolTable()`, eliminating the LLVM 23 ELF backend crash on live `@selector(...)` references (`tests/plugin/positive/test_valid_comdat_selector_guard.m`, `e2e_sealed_polymorphism_tests`).
- [x] **6.0.3 Runtime Archive Hygiene (`rt/src/veriobjc_runtime.c`, `CMakeLists.txt`):**
  Guarded the internal `veriobjc_runtime_*_test` disassembly probe wrappers behind `#ifdef VERIOBJC_ASM_PROBE`, added freestanding anchor `veriobjc_rt_abi_version`, and shipped `veriobjc_rt_static` (`libveriobjc_rt.a`, `-ffreestanding -mno-sse`) via CMake so `rt/src/veriobjc_runtime.c` compiles cleanly under `veriobjc-check`.
- [x] **6.0.4 Reject Unknown `annotate("veriobjc_*")` / `annotate("objc_*")` Spellings (`compiler/src/AstBanChecks.cpp`):**
  Implemented `VeriObjCVisitor::VisitDecl` allowlist verification emitting Goose-style diagnostics with canonical macro suggestions (`veriobjc_cold`, `veriobjc_pure`, `veriobjc_nolatency`, `veriobjc_export`) on any unrecognized `veriobjc_*` / `objc_*` `AnnotateAttr` (`test_banned_unknown_veriobjc_annotate.m`, `valid_objc_unknown_veriobjc_annotate.m`, `test_valid_canonical_annotations.m`).
- [x] **6.0.5 `@cold` Static Staging File Loader (`host/include/veriobjc_host_io.h`):**
  Implemented `veriobjc_host_load_file_staged` (`@cold @io @blocking`, `#if __STDC_HOSTED__`) backed strictly by caller-supplied static staging memory (returning `objc_vresult`-checked `veriobjc_host_io_result` + `veriobjc_host_slice_as_span` conversion to `vspan`, rejecting missing/empty/oversized files fail-closed with zero `malloc`/`free`) while preserving `<vstd/io.h>`'s freestanding zero-POSIX invariant (`TestHostIo.cpp`, `test_valid_host_staged_io.m`, `test_banned_host_io_in_default_fn.m`, `examples/host_staged_file_loader.m`, `valid_objc_host_io_outside_cold.m`).
- [x] **6.0.6 Domain-Neutral `-isystem` Freestanding C99 Header Interop Test (`tests/plugin/isystem_ext/synth_c99_frame.h`):**
  Added a synthetic single-file C99 frame header (statement-expression endian macros, C casts, `static inline` frame unpacker, and unannotated `extern` C prototype) included via `-isystem`, refined `VisitStmtExpr` (`isInSystemMacro`) and `CfgDataflowChecks` (`PointeeProv` separation for pointer fields vs embedded arrays), and verified both positive `@cold` staging + `VERIOBJC_SCOPED_STACK` handoff and negative `@nolatency` / adversarial pointee-escape rejections (`test_valid_isystem_c99_interop.m`, `test_banned_isystem_c99_in_nolatency.m`, `test_banned_escape_via_pointee_field.m`, `examples/isystem_c99_frame_stage.m`, `valid_objc_isystem_extern_in_nolatency.m`).
- [x] **6.0.7 Deterministic Freestanding Cohesion Gate (`tests/integration/check_vstd_cohesion.sh`, `scripts/install.sh`):**
  Eliminated the `SIGPIPE`/`pipefail` race with Clang builtin `<stddef.h>` (`long double`) by filtering `clang-23 -E` output via `# <line> "<file>"` markers down to repository headers only, verified a negative self-test injecting `float` into a temporary `rt` header copy fails closed (`rc != 0`), verified 25/25 consecutive runs pass deterministically, and fixed `libclang-${LLVM_VERSION}-dev` / `libclang-cpp${LLVM_VERSION}-dev` package names in `scripts/install.sh` & `INSTALL.md`.
- [x] **6.0.8 Transitive Helper-Sink Escape & Struct-Field Alias Use-After-Move (`compiler/src/CfgDataflowChecks.cpp`):**
  Extended interprocedural helper parameter summaries (`HelperParamSummary`) to track global and out-parameter escape sinks transitively across helper call chains (`test_banned_escape_via_helper_global.m`, `valid_objc_escape_via_helper_global.m`), and tracked `LinearTarget` (`(RootVD, FieldDecl*)`) alias provenance + `MemberExpr` move invalidation so reading through `&s` / `&s.field` after moving `s` or `s.field` fails closed (`test_banned_use_after_move_field_alias.m`, `valid_objc_use_after_move_field_alias.m`).
- [x] **6.0.9 Fail-Closed Zero-DSP Hardware Kernel Call Graph (`compiler/src/CfgDataflowChecks.cpp`):**
  Rejected indirect calls (function pointers / blocks) and bodyless non-`objc_vconstexpr` `extern` callees reachable directly or transitively from any `objc_hw_kernel` (`test_banned_hw_kernel_fnptr_call.m`, `test_banned_hw_kernel_extern_call.m`, `valid_objc_hw_kernel_fnptr_call.m`, `valid_objc_hw_kernel_extern_call.m`).
- [x] **6.0.10 Ban Raw Pointer Casts to `@sealed` / `@final` Classes (`compiler/src/AstBanChecks.cpp`):**
  Banned forging untagged `@sealed` / `@final` object pointers via raw pointer casts or unchecked downcasts outside `veriobjc_alloc_sealed` (allowing only `veriobjc_sealed_alloc` with matching `sizeof(Cls)`, valid derived-to-base Objective-C upcasts, and null pointer constants) so `veriobjc_sealed_tag(obj)` never reads out-of-bounds memory behind raw buffers (`test_banned_raw_cast_to_sealed.m`, `valid_objc_raw_cast_to_sealed.m`, `examples/vvector_inplace_batch.m`).
- [ ] **Task 6.0: `vosh` — Self-Hosted Zero-Boilerplate Scripting & Verification Runner (`tools/vosh/`)**
  - Written in **VeriObjC itself** (compiled in Stage 2 of CMake using `clang-23` + `libVeriObjCPlugin.so`, audited with `--effects-report` proving 0 allocations/syscalls outside `vosh_os_posix.m` `@cold @io` quarantine).
  - Supports `#!/usr/bin/env vosh` shebang execution, `#line`-preserving top-level snippet wrapping (pre-wired `VERIOBJC_SCOPED_STACK` scratch arena), and FNV-1a-64 content-addressed binary caching (`~/.cache/vosh/`) for millisecond re-runs.
  - Supports inline `//!` verification pragmas (`//! verify: smt, effects, branch`, `//! max-survivors: 0`) and `vosh --inspect <script.m>` TUI summary displaying Z3 `QF_BV` proof status, surviving effects, unproven branches, and `nm` zero-dispatch/zero-heap audit.

### Block 6.1 — Package Manager & Build Budget Runner (`vpm`)

- [ ] **Task 6.1: `vpm` Bootstrap & Content-Addressed Artifact Store**
  - Standalone CLI tool; pins the Clang/LLVM toolchain and manages a content-addressed store for verified `VOBJ` v1 snapshot images and build manifests (producer-agnostic, zero external repo coupling).
- [ ] **Task 6.2: Package Manifest & Compile-Time Budget Enforcement**
  - Enforces per-package instantiation bytes (Task 2.8.5), `vregex` DFA table bytes, and static arena budgets at build time.
- [ ] **Task 6.3: CI Survivor Report Archival & Diffing**
  - Automates per-commit `--effects-report`, `--branch-report`, and `--mono-report` artifact diffing via `rt_survivor_diff_audit` (Task 4.2).

---
## 🧵 Phase 7 (Draft): Low-Latency Library Kit (7a), Reference Demonstration (7b), Language-Property Hardening (7c), Spec/Stability Governance (7d) & Computational-Finance Lessons (7e)
*(Informed by real-world "wrap-the-OS" seam engineering practice (time, user-space networking, CPU pinning, custom memory allocators, busy-poll event loops, async persistence): the first tier is **library-only** — headers over already-shipped `rt/`/`vstd/`/`sys0/` primitives with zero compiler changes — so anyone can assemble a low-latency system from working examples; a reference application (Block 7b) dogfoods the kit end-to-end and produces the evidence; language-property hardening (Block 7c) is gated on that evidence rather than on waiting for external adoption. Type-vs-check scorecard: capability descriptors become opaque types now; `@nolatency` is documented as an effect on the function type (promotion deferred to 7c); `@steady`, `lmax`, and `@spin` remain checks; `vticks`/`vns` are already types. Per-task red/green with ban-corpus rows unchanged; annotations are earned by library usage, not designed upfront. Block 7d (dialect-spec freeze, stability governance, and registry/ABI CI automation, inspired by Hare's spec-first and release-policy discipline) is **order-independent** — it may start in any wave ahead of 7a–7c, and Block 7c promotions additionally require a ≥Stable tier grade from Task 7d.2.)*

### Block 7a — Low-Latency Library Kit (zero compiler changes; `<sys0/`, `<vstd/`, `host/`)

- [ ] **Task 7a.1: Boot Calibration & Typed Clock Domains (`<sys0/time.h>`)**
  - One-time `@cold` calibration pinning the cycles→nanoseconds multiplier/offset at boot; strong types (`vticks`, `vns`, `<vstd/strong.h>` discipline) make cross-clock-domain mixing a compile error; documented drift/resolution contract per platform.
  - All unit tests run on an injected virtual tick source instead of wall time (deterministic under simulation).
- [ ] **Task 7a.2: Opaque Boot Capability Types (`vcore_set`, `varena_cap`, `vmap_cap`)**
  - Nominal opaque structs with a private `payload_DoNotAccessDirectly` field: direct access rejection **reuses the existing `vatomic` plugin ban** (zero new mechanism) and only `@cold @io` boot functions (`<sys0/mem.h>` / `<sys0/mmu.h>`, platform shims) may produce them — pinned core sets, statically reserved arenas, hugepage/locked regions.
  - Freestanding fallback: plain `veriobjc_stack` backing so CI stays hosted-toolchain-free and deterministic (mirrors the pure-CPU ternary fallback pattern, Task 5.7).
  - Ban-corpus rows: `tests/plugin/negative/test_banned_capability_direct_access.m`, `test_banned_capability_forged_init.m`; valid: `test_valid_capability_boot_to_steady.m`.
- [ ] **Task 7a.3: Hierarchical Timing Wheel & Deadlines (`<vstd/timer.h>`)**
  - Constant-bounds timing wheel over `sys0_rdtsc` ticks; deadline objects consumable by the runloop and `veriobjc_spin_until`-style bounded waits; overflow/underflow fail closed with named diagnostics.
- [ ] **Task 7a.4: Step-Based Runloop & Pollables (`<vstd/runloop.h>`)**
  - The busy-poll event-loop engine as headers over the shipped Task 3.8 (`vspsc_channel`) / Task 3.11 (`vdma_ring`) / Task 7a.3 primitives: pollables compose into a deterministic `step()` loop whose per-step work is bounded (constant trip counts or `lmax`-documented bounds documented in the header contract).
  - `@blocking` / `@io` / `@alloc` primitives inside the composed steady path are already rejected by the shipped Task 2.8.1 checks — the kit turns the "hot loop never contacts the OS" convention into normal library usage.
- [ ] **Task 7a.5: Ring Traffic Journal & Bit-Exact Replay (`<vstd/journal.h>`, `host/`)**
  - Content-addressed journal of SPSC-channel / DMA-ring events (VOBJ v1-style framing + FNV-1a, `veriobjc_tape_header` discipline); overhead bounded and self-declared in the header contract.
  - Host replay harness: re-run from a journal snapshot and bit-compare the final `VOBJ v1` state; accept/reject fixture corpus `fixtures/journal_v1/{accept,reject}` with `.bin` + `.json` expectations (mirrors the Task 5.6b discipline); replay divergence is a named, fail-closed diagnostic.
  - *Tier note:* in 7a, determinism is a **tested convention** (fixtures catch drift); the checked-property version is 7c.4.
- [ ] **Task 7a.6: `vpm init --template=lowlatency` Starter Skeleton (rides Task 6.1)**
  - A working end-to-end example composing 7a.1–7a.5: boot phase (`@cold` staging + capability reservation) → steady runloop with ring pair + timing wheel → journaling + offline replay; the onboarding artifact for "anyone can start a low-latency project."

### Block 7b — Reference Demonstration: HFT-Shaped Feed-Handler Pipeline (Dogfooding the Kit)

Rather than waiting for external adoption to validate Block 7a, the project builds its own demonstration: a self-contained, deterministic, HFT-shaped low-latency application assembled strictly from the 7a + 7e kit pieces. Its outputs — measurements, audit artifacts, and a friction log — are the evidence that gates Block 7c. (Portfolio precedent: the `vgeneric` frictions document, VG-001…VG-006; Phase 5 end-to-end integration examples, including the Task 5.2 `order_book_snapshot` lineage.)

- [ ] **Task 7b.1: Reference Application Definition & Steady-Path Core (`examples/lowlatency/pipeline.m`)**
  - A deterministic market-data feed decoder → order-book state machine → outbound ring pipeline: the domain the OS-seam practices come from, composed strictly from 7a + 7e kit pieces (7e.1 blocklist as the book's backing store, 7e.4 batch drain for cold-side work).
  - Steady path: capability-reserved arena (7a.2), runloop `step()` with ring pair + timing-wheel deadlines (7a.3/7a.4), journaling (7a.5), virtual tick clock (7a.1) for determinism.
  - Boot phase confined to `@cold @io` staging + capability reservation; the steady/core path stays freestanding-safe.
- [ ] **Task 7b.2: Friction Log (`docs/veriobjc-lowlatency-frictions.md`)**
  - Every sharp edge hit while building the pipeline gets a numbered, typed entry (LG-001…) recommending **promote / defer / reject** per Block 7c candidate; no silent workarounds (same discipline as the vgeneric frictions document).
- [ ] **Task 7b.3: Measurement & Audit Artifacts (Gate Evidence)**
  - Bench harness + latency gate (Tasks 4.1 / 4.3) p50/p99/p99.9 artifacts at a declared budget; steady-path audit trifecta (`nm` zero libc/syscall symbols, `llvm-objdump -d`, coverage); `--effects-report` diff clean (Tasks 4.2 / 6.3).
  - Full-pipeline journal round-trip: run → journal → replay bit-exact, adding a pipeline golden to the `fixtures/journal_v1/` corpus (extends 7a.5).
- [ ] **Task 7b.4: Block 7c Justification Memo**
  - A written verdict per 7c candidate (effects → function types, `@steady` module gate, `lmax`/`@spin` checks, compiler-attested determinism) argued only from 7b evidence; a 7c task enters planning **only** when its memo row says "promote." Fail-closed by default: absence of evidence = defer.

**Two-pass measurement model:** **v1** (this block) validates the 7a/7e library tier with the existing checks and feeds the 7b.4 memo; **v2** (Task 7b.5, after Phase 8) re-runs the same examples on the refinement-typed hot paths, so the v1→v2 artifact diff becomes Phase 8's zero-runtime-erasure evidence on a measured pipeline.

- [ ] **Task 7b.5: Post-Phase-8 Re-Baseline (the v2 measurement pass)**
  - Once Phase 8 Blocks 8.1–8.3 land: re-run the measured examples (and the pipeline) with `veriobjc_where` / `veriobjc_index` in the hot loops — same inputs, same declared budgets.
  - Commit the v1→v2 artifact diff (bounds branches eliminated from the assembly, outputs byte-identical, cycle delta) as proof of zero-runtime-erasure on a real measured pipeline; extends Task 8.3.2's audit from a synthetic example to a full artifact.

#### Examples roadmap (`examples/lowlatency/`; owned by Block 7b; wired into `run_examples_smoke_test.sh`, Task 5.1 lineage)

Each example validates one belief veriObjC makes about the world, carries a one-line provenance comment naming its motivating source (all designs veriObjC-native — domain inspiration only, zero ported code).

1. **`blocklist_orderbook`** (exercises 7e.1; continues the Task 5.2 `order_book_snapshot` lineage) — *v1*: price-level order book on `vblocklist` — growth-past-blocks pointer-survival audit, mark/rewind intraday scrollbacks, `VOBJ v1` snapshot round-trip re-fuzzed byte-by-byte. *Belief tested: pointer-stable growth without relocation is enforceable; arena wipe beats free; snapshots are mutation-hostile.*
2. **`arena_trace_sweep`** (exercises 7e.1 + Task 2.8; motivator: Ch. 10 tape discipline) — *v1*: write-once op trace on the steady arena — forward record, backward zero-allocation sweep against a mark. *Belief tested: the arena's memory model natively supports evaluate-then-adjoint-sweep; `nm` proves nothing ever left the data stack.*
3. **`false_sharing_bench`** (exercises 7e.2; motivator: Ch. 3.5 postmortem) **【measured: v1, re-measured v2】**: unpadded vs cache-line-padded partitioned counters through the Task 4.1/4.3 gate. *Belief tested: the padding annotation rule is worth having; the delta is the teaching number.*
4. **`pipeline_batch_drain`** (exercises 7e.4; ships with its negative-corpus pair) — *v1*: hot steady runloop + `@cold @io`-quarantined OS-blocking drain worker over SPSC channels + return channel. *Belief tested: all waiting lives outside the hot path, and the plugin proves it.*
5. **`pipeline_checkpoint_replay`** (exercises 7e.3) **【measured: may land in v2 — benefits most from 8.3.1 ghost indices】**: N virtual ticks → mark snapshots → reload mid-run checkpoint → bit-exact divergence-free replay against `fixtures/journal_v1/checkpoint/` goldens. *Belief tested: the world can be rewound to any mark, deterministically — the flagship demo.*
6. **`parallel_partitioned_determinism`** (exercises 7e.5; motivator: Ch. 7 skip-ahead/partitioned-randomness design) **【measured: v1, re-measured v2】**: four workers, disjoint `VERIOBJC_SCOPED_STACK` arenas, worker-index-seeded inputs; 4-worker output bit-identical to the serial run; zero locks, `nm`-audited. *Belief tested: determinism by construction, not synchronization — direct 7c.4 evidence.*
7. **`model_product_static_dispatch`** (exercises Task 2.3; motivator: Ch. 6 template-method model/product decoupling) — *any wave*: the same decoupling shape via `@sealed`/`@final` + static dispatch lowering. *Belief tested: object-oriented structure survives with zero-dispatch, zero-runtime code (`llvm-objdump -d` sweep).*

*Build order: v1 — 1 and 4 first (the pipeline's components), then 2; measured examples land in their marked pass; provenance notes attribute the motivating chapter without embedding sourced code.*

### Block 7c — Language-Property Hardening (gated on Block 7b evidence)

- [ ] **Task 7c.1: Effect-Carrying Function Types (promote `@nolatency` from attribute to function-type property — gated by the 7b.4 memo)**
  - Function types carry effect bounds (Koka-style); transitive callee restriction then falls out of ordinary type compatibility, and the shipped Task 2.8.1 call-graph check becomes the migration bridge rather than a new enforcement.
- [ ] **Task 7c.2: `@steady` Module-Level Phase Gate (gated by the 7b.4 memo)**
  - A module-level qualifier (nearest analogue: `no_std`) turning the boot→steady one-way barrier into a checked property: after the barrier, OS-facing operations are rejected statically with the diagnostic naming the `@cold @io` module that must own them; capability values (7a.2) are the only sanctioned boot→steady channel.
  - The checked conventions may ship first as optional ban-corpus rows during 7a/7b: `test_banned_steady_thread_spawn.m`, `test_banned_steady_sched_affinity.m`, `test_banned_steady_sleep.m`, `test_banned_steady_cold_call.m`.
- [ ] **Task 7c.3: Bounded-Loop & Bounded-Spin Checks Promoted (optional — gated by the 7b.4 memo)**
  - `lmax` loop bounds and the `@spin` worst-case budget promote from documented 7a.4 header contracts to enforced checks in `CfgDataflowChecks.cpp` (precedent: the `vasync_task_is_ready` busy-wait ban, Task 3.12); ban-corpus rows unchanged in name.
- [ ] **Task 7c.4: Determinism as Checked Property (optional — gated by the 7b.4 memo)**
  - For programs whose hot path is effect-bounded (7c.1) and whose time reads flow through injected tick sources: registered journaling + replay verifiability becomes a compiler-attested property, closing the tier gap left open by 7a.5.

### Block 7d — Dialect Specification, Stability Governance & Registry Automation (order-independent; Hare-inspired)

*(Rationale: Hare's most transferable practices are its normative language specification, its written release policy, and freestanding-as-first-class documentation. veriObjC today *is* `plan.md` + docs + the Task 2.7.1 ban registry; this block turns that into governed, CI-checked artifacts so the verification claim outlives any single contributor or model agent.)*

- [ ] **Task 7d.1: `VeriObjC Dialect Specification v0` (`docs/spec/dialect-v0/`)**
  - Normative doc set for the accepted language subset: each plugin check recorded as a machine-readable descriptor (check id, scope: AST/CFG/IR/link-time, predicate summary, diagnostic id + exact rendered text, ban-corpus test ids) with prose tables generated *from* the descriptors (single source of truth, no drift between prose and registry).
  - Freeze ritual mirrors the TTAPE v1.0 discipline: re-derivable content is generated; hand-written parts are owned; supersession adds versions, never edits in place.
- [ ] **Task 7d.2: Stability Matrix & Release Policy (`docs/stability.md`)**
  - Four tiers per subsystem/decision point — **Frozen / Stable / Experimental / Provisional** — over `rt/` headers, `vstd/` contract namespaces, `sys0/`, plugin checks, `veriobjc-cc` flags, and file-format artifacts (`VOBJ v1`; fixture corpora accompany their tier).
  - Written promise rules (what migrates and what may break between releases); the README's "clearly marked experimental" markings point here; a machine-readable manifest (`stability.yaml`) expresses grades for CI consumption.
- [ ] **Task 7d.3: Diagnostic & Ban-Registry Compliance Automation (`tests/integration/check_diag_registry.sh`)**
  - CI script hardening the Task 2.7.1 manual mapping into enforced consistency: (a) every registry/descriptor row binds ≥1 negative-test file; (b) every negative test is run and its emitted diagnostic text compared to the registered text (fail on drift); (c) unreferenced or test-orphaned diagnostic ids fail closed.
- [ ] **Task 7d.4: ABI & Symbol Freeze Audit (`tests/integration/check_rt_symbols.sh`)**
  - Golden `llvm-nm` symbol lists for `libveriobjc_rt.a`: exported surface + required *absences* (no libc/malloc, no `objc_msgSend`/runtime dynamic symbols, per the Task 1.2/6.0.3 audit precedent, replacing ad-hoc audits with a committed gate); CI fails closed on any drift; a symbol-surface change requires an explicit task touching the golden and bumping `veriobjc_rt_abi_version`.
- [ ] **Task 7d.5: Bare-Metal Chapter & Freestanding CI Job**
  - First-class user documentation (Hare's "Freestanding programs" practice): `docs/writing-for-bare-metal.md` — the `sys0/` hardware layer, `host/` isolation, `@cold` staging, and capability patterns written for *users*, not plan archaeology.
  - CI job compiling the composed steady-path examples with hosted headers excluded (`-ffreestanding -mno-sse` strictness) so the freestanding story is continuously proven, not asserted.
- [ ] **Task 7d.6: Stability-Grade CI Guard (optional)**
  - Lint over `stability.yaml` + includes: a `Stable`/`Frozen`-tier header consuming an `Experimental`-tier symbol fails CI ("no upward vegetables"); ties Task 6.3 report archival into tier-diff reports.

### Block 7e — Computational-Finance Lessons Applied (rides Blocks 7a/7b; source: Antoine Savine, *Modern Computational Finance: AAD and Parallel Simulations*, Wiley 2019 — Ch. 3 parallel C++, Ch. 10 AAD tape memory management, Ch. 13 check-pointing, Ch. 15 expression templates)

*(Rationale: quantitative-finance low-latency practice independently arrived at several of veriObjC's allocator shapes — the book's write-once, mark/rewind, wholesale-wiped tape is functionally the arena + one-store scope reset design — and supplies four concrete, testable additions the kit lacks. Examples exercising these additions live in Block 7b's roadmap; 7e's own acceptance is unit-level tests + ban-corpus rows. All tasks follow the standing red/green + audit-trifecta discipline.)*

- [ ] **Task 7e.1: Pointer-Stable Growable Blocklist (`<vstd/blocklist.h>`)**
  - Segmented storage (list of fixed-capacity blocks) with three proven invariants from the AAD tape design: appending a block **never relocates** existing elements (pointers/iterators stay valid), no per-element destruction (wholesale wipe with the container), and **O(1) mark/rewind** mirroring `veriobjc_stack` scope marks.
  - Fills the gap between `veriobjc_stack` (O(1) reset, no growth) and `veriobjc_pool` (fixed slots, no pointer-stable growth); relocation-free backing for the 7a.5 journal and any growable steady-state structure.
  - Plugin-checked with existing machinery: `ContainerChecks.cpp` verifies capacity ceilings (`BlockSize`, `MaxBlocks`); the Task 2.4.2 escape lattice must reject storing pointers/iterators into a blocklist across a rewind (new ban-corpus rows: `test_banned_blocklist_ptr_across_rewind.m`, `test_banned_blocklist_capacity_overflow.m`; valid: `test_valid_blocklist_grow_mark_rewind.m`).
  - E2E: grow past several blocks with an all-pointers-survive audit, then mark/rewind + one-store disassembly audit (Task 5.5 pattern).
- [ ] **Task 7e.2: False-Sharing Discipline (`<vstd/pad.h>` + annotation rule + Phase 4 experiment)**
  - `valign_cacheline` primitive (documented 64-byte default, target-overridable) plus a compile-time rule: a struct annotated as cross-thread partitioned (via the Task 6.0.4 annotation allowlist) must have its per-thread partitions cache-line padded — mechanically checkable where hand-tuned C++ relies on convention.
  - Red/green Phase 4 experiment replicating the book's parallel-partitioned-counter postmortem: unpadded vs padded variants through the Task 4.1/4.3 harness, p50/p99/p99.9 artifacts committed ("no number, no claim"); the experiment is the teaching artifact for 7b.
- [ ] **Task 7e.3: Checkpointed Journal Replay (extends 7a.5)**
  - Journal marks emit `VOBJ v1` snapshots; the host replay harness supports replay-from-any-checkpoint (O(since mark), not O(full history)); each checkpoint chain (mark → snapshot checksum) verified fail-closed.
  - New goldens: `fixtures/journal_v1/checkpoint/` (`accept`/`reject`, `.bin` + `.json`); divergence = named diagnostic. Gives the 7b pipeline its strongest demo: rewind the order book to any mark and replay deterministically.
- [ ] **Task 7e.4: Batch Executor on the Cold Side of the Barrier (`<vstd/batch.h>`)**
  - The steady path submits work items over SPSC channels; a deliberately OS-blocking worker (condvar/OS waits allowed **only** inside its `@cold @io` quarantined module, Task 6.0.5/2.8.2 precedent) drains and processes, then reports back over a return channel.
  - Proves the boot→steady barrier operates in both directions and gives the 7b pipeline its realistic topology: hot loop + slow drain thread, no blocking primitives outside quarantine.
  - Ban-corpus rows: `test_banned_batch_wait_in_steady.m`, `test_banned_batch_submit_blocking.m`; valid: `test_valid_batch_drain_quarantine.m`.
- [ ] **Task 7e.5: Parallel Determinism by Input Partitioning (extends the 7c.4 design story)**
  - Parallel workers own disjoint arena storage regions (per-worker `VERIOBJC_SCOPED_STACK` arenas, Task 1.1 pattern) and derive their inputs/randomness from worker index (skip-ahead-seeded generators); zero shared mutable state outside channels/atomics (Task 3.8 enforcement) — parallel results are bit-exact **without locks**.
  - Evidence written into the 7b.4 memo as a 7c.4 candidate: parallel deterministic replay becomes reachable by construction, not synchronization.
- [ ] **Task 7e.6 (optional, friction-log-gated): Zero-Temporary Combinators over Spans (`<vstd/expr.h>`)**
  - Expression-template-inspired map/fill/fold combinators over `vspan`/`vstream`: static dispatch only (no vtables), zero intermediate allocations (escape lattice guarantees span rooting), composable with the Task 3.13 SIMD layer.
  - **Built only if the 7b friction log shows the pipeline needs it** (no decorative code; Task 3.15 cohesion gate applies).

*(Ordering: Block 7a is independent of Block 6.0 except 7a.6 riding Task 6.1; 7a.1 → 7a.3; 7a.5 feeds 7b.3 — 7e.1 and 7e.3 extend it; Block 7b gates Block 7c per the 7b.4 memo, with 7e.4 supplying the 7b pipeline topology and 7e.5 supplying 7c.4 design evidence; Block 7d is order-independent; the examples' v2 re-baseline (7b.5) follows Phase 8 Blocks 8.1–8.3. Wave-boundary review artifacts apply per the standing process rule.)*

---

## 🧬 Phase 8 (Draft): SMT-Backed Refinement & Index Types (`QF_BV`) — Zero-Runtime-Erasure Dependent-Type Ergonomics
*(Mapped to: `docs/veriobjc-type-system-and-metaprogramming-vmeta.md` & `compiler/src/SmtContractVerifier.cpp`. Origin: external design proposal, amended in review; sections below preserve the original task structure with inline *(Amendment: …)* design notes.)*

**Decision in review — middle ground only.** Full dependent types (Lean/Rocq-style) are rejected as contradicting four load-bearing design decisions: (1) *no manual proofs* — dependent types make the programmer write proof terms while the compiler merely checks; veriObjC's bet is the inverse (obligations discharge automatically via Z3 or fail closed); (2) *decidability as a boundary* — type-level computation under full dependency needs termination and universe machinery, the opposite of the fuel-bounded, fail-closed-on-the-undecidable pattern (`vconstexpr`, Task 2.5.5) — `QF_BV`'s decidability is what makes "fail closed" meaningful; (3) *mutability* — dependency over in-place mutation forces ownership/borrow machinery into Objective-C; the `Folded`/`Unfolded` discipline (Block 8.2) is the refinement world's answer (Liquid Haskell's substitution/scrutinee shape); (4) *the plugin architecture* — a dependent-type elaborator is a second compiler, not a plugin. Industrial precedent supports the middle ground: Liquid Haskell, SPARK Ada predicate aspects, Dafny, Rust Flux/Refine — SMT discharges obligations silently, with essentially no industrial C-family deployment of full dependent types.

**Siting & gates (amendment):** Phase 8 is a *sibling of the SMT track*, not dependent on Blocks 7a–7c; parts of it precede Phase 7 items. Two hard gates: (1) every refinement diagnostic enters the Task 7d.1 descriptor registry and passes the Task 7d.3 automated compliance gate — this is the largest diagnostic-vocabulary expansion the dialect has taken, and it must be born pre-wired or the dialect spec is stale on arrival; (2) Block 8.0 (reconciliation) runs first so existing special-purpose checks demote onto the refinement machinery instead of doubling enforcement paths.

### Block 8.0 — Reconciliation of Existing Bounds Machinery (amendment; runs first)

- [ ] **Task 8.0.1: Demote or Deduplicate Existing Bounds/Index Checks onto Refinements**
  - Inventory each special-purpose mechanism as either a refinement-obligation instance or a knowingly kept separate check (rationale filed in the 7d.1 descriptor registry): 7c.3 `// lmax: N` loop bounds (special case: constant range refinements), 7a.4 runloop step-bounds contracts, 7e.1 blocklist index obligations (`i < MaxBlocks * BlockSize`), Task 2.6.1 context caps.
  - Duplicated enforcement paths for one property are rejected; the survivor is named in the registry.

### Block 8.1 — Scalar & Pointer `QF_BV` Refinement Types (`veriobjc_where`)

- [ ] **Task 8.1.1: Type-Level Refinement Attribute & Expression Parser (`AstBanChecks.cpp` / `SmtContractVerifier.cpp`)**
    - Register canonical `veriobjc_where("<qf_bv_expr>")` macro/attribute spelling in the `VeriObjCVisitor::VisitDecl` allowlist (extending Block 6.0.4) for `TypedefNameDecl`, `ParmVarDecl`, and `FieldDecl` over scalar integer and pointer carrier types (`uint8_t`–`uint64_t`, `int8_t`–`int64_t`, `size_t`, `uintptr_t`, raw pointers).
    - Parse `self`-bound predicates (e.g., `self < 4096`, `(self & 7) == 0`, `self >= 8 && self <= 1500`) into the existing `QF_BV` AST/SMT expression builder, rejecting non-`QF_BV` constructs (quantifiers, floats, impure calls) fail-closed with a named diagnostic (`test_banned_refinement_non_qfbv.m`, `valid_objc_refinement_non_qfbv.m`).
- [ ] **Task 8.1.2: Refinement Propagation Across `auto` & `vgeneric` (`CfgDataflowChecks.cpp` — amended design)**
    - **Amendment:** implement refinements as *facts in the flow environment* (`Γ` entries in the existing `CfgDataflowChecks.cpp` lattice, alongside `StackScoped`/move state), not as `TypedefType`/`AttributedType` sugar that must survive Clang's `ASTContext` canonicalization. The `veriobjc_where` attribute *seeds* `Γ` at declaration; propagation across `auto` (Task 2.5.7), ternaries, and `vgeneric` specializations (Task 2.5.4) then falls out of ordinary flow tracking rather than fighting canonical-type erasure.
    - Mutation-invalidation is thereby automatic: reassigning a witness variable re-fires its obligation (`test_valid_refinement_auto_propagation.m`, `test_banned_refinement_witness_mutation.m`).
- [ ] **Task 8.1.3: Subtyping & Implicit Assignment Discharge via Z3 `QF_BV` (`SmtContractVerifier.cpp`)**
    - Emit automatic `QF_BV` implication checks (`Γ ∧ P_actual(v) ⇒ P_expected(v)`) at assignment, return, and call-argument boundaries whenever a plain or refined value flows into a refined target type.
    - Integrate path-sensitive branch assumptions from `clang::CFG` (e.g., `if (idx < cap)`) into `Γ` so checked runtime guards discharge refinement subtyping automatically without redundant casts (`test_valid_refinement_branch_subtyping.m`, `test_banned_refinement_subtyping_violation.m`, `examples/banned/valid_objc_refinement_overflow.m`).
    - *(Amendment: obligation *budget + memoized discharge* per predicate pair — compile-time scale guard in the Task 2.8.5 mono-budget tradition; discharges are deduplicated so a pathological call graph cannot trigger per-boundary re-proving.)*

### Block 8.2 — Struct Field Refinements & Folded/Unfolded Typestate (`CfgDataflowChecks.cpp`)

- [ ] **Task 8.2.1: Dependent Struct Field Predicates (`FieldDecl` Cross-Field `QF_BV` Invariants)**
    - Support `veriobjc_where` predicates on `@flat` / `@sealed` struct fields referencing sibling fields declared earlier in the same struct (e.g., `uint32_t cap; uint32_t head __attribute__((veriobjc_where("self < cap")));`).
    - Enforce structural well-formedness: reject cyclic or forward-referencing field predicates at compile time (`test_banned_refinement_cyclic_field.m`).
    - *(Amendment: predicates must be layout-order-stable — a field reorder across versions that breaks a sibling predicate is rejected; predicate descriptors record the field order they assume.)*
- [ ] **Task 8.2.2: `clang::CFG` Folded / Unfolded Struct Mutation Tracking (`CfgDataflowChecks.cpp`)**
    - Extend the dataflow lattice with `StructInvariantState` (`Folded` vs. `Unfolded(dirty_fields)`) to support imperative multi-field updates without false-positive mid-update failures.
    - When a refined struct field is mutated, transition the struct variable to `Unfolded`, forbid passing it to functions expecting a `Folded` refined struct, and automatically re-verify all cross-field `QF_BV` predicates at the next sequence/call boundary or scope exit to transition back to `Folded` (`test_valid_struct_field_repack.m`, `test_banned_call_with_unfolded_struct.m`).
    - *(Amendment: soundness edge named in advance — a `@nolatency` callee must reject `Unfolded` struct arguments even if it does not read refined fields, unless the callee's own contract is discharged; and `Unfolded` recheck volume is bounded by the Task 8.1.3 obligation budget.)*

### Block 8.3 — Ghost Index Types & Dependent Pairs (`vspan<T, n>` / `from_bytes` Erasure)

- [ ] **Task 8.3.1: Ghost Symbolic Length & Alignment Indices (`<vstd/span.h>`, `<vstd/stack.h>`)**
    - Introduce compile-time ghost index annotations (`veriobjc_index(sym_expr)`) for `vspan` and `vstack_array` views, binding symbolic lengths and alignment guarantees to in-scope integer variables or constants without emitting VLA stack allocations.
    - Statically discharge `vspan` indexing (`span[i]`), slicing, and `zip` length-equality obligations via `QF_BV` (`i < len`, `lhs.len == rhs.len`), eliminating runtime bounds branches inside `@nolatency` loops (`test_valid_indexed_vspan_zero_bounds.m`, `test_banned_indexed_vspan_oob.m`).
    - *(Amendment: witness invalidation must be explicit — if the bound variable mutates or escapes scope, the view's obligations re-fire or the view is invalid; handled by the Task 8.1.2 flow environment, with dedicated ban rows for escaped witnesses: `test_banned_index_witness_escape.m`. This is the highest-payoff/lowest-tolerance item: bounds-branch elimination is real p99 money on the `@nolatency` path.)*
- [ ] **Task 8.3.2: Dependent-Pair (`Σ`-Type) Wire Validation Bridge (`veriobjc_from_bytes`)**
    - Extend `veriobjc_from_bytes` / `objc_vresult` parser patterns so a single boundary check on raw DMA/VOBJ bytes returns a validated dependent-pair carrier bundling a refined/indexed view (`vspan` with proven `len >= HeaderSize` and alignment) with its symbolic length witness ("parse, don't validate", with the witness carried in a flat carrier).
    - Audit via `llvm-objdump -d` and `nm` that all `veriobjc_where` predicates and ghost indices erase to zero machine instructions and zero runtime symbols (`examples/refined_market_data_parser.m`, `test_e2e_refinement_zero_overhead.m`).

*(Phase-8 ordering: 8.0 → 8.1 → 8.3; Block 8.2 rides 8.1.2's flow-environment design; 8.3.1's discharge supersedes the reconciled 7c.3/7a.4/7e.1 mechanisms per Block 8.0's registry decision. Gates: Task 7d.1/7d.3 registry compliance for every new diagnostic; Task 7d.2 stability grades for the `veriobjc_where`/`veriobjc_index` attributes; the Task 8.3.2 zero-overhead `llvm-objdump`/`nm` audit is a standing acceptance criterion for the whole phase.)*