# 🗺️ VeriObjC Master Implementation & TDD Execution Plan (`plan.md`)

This living checklist tracks the complete execution of **VeriObjC (Verifiable Objective-C)** across its modular compiler plugin (`compiler/`), freestanding micro-runtime (`rt/`), standard library (`vstd/`), and host verification harnesses (`host/`).

Every implementation step follows strict **Test-Driven Development (TDD: Red ➔ Green ➔ Refactor ➔ Audit)** governed by [`AGENTS.md`](./AGENTS.md) and the specifications in [`docs/`](./docs/).

---

## 🧭 Progress Dashboard

* **Overall Status:** Phases 0–3 & Phase 5 Complete; Phase 4 Near-Complete (**Next Up:** Task `4.4` External Z3/SMT Solver Discharge, before Phase 6)
* **Compiler Substrate:** Clang / LLVM 23.1.5 (`clang-23`), split cleanly across `PluginMain.cpp`, `AstBanChecks.cpp`, `CfgDataflowChecks.cpp`, `ContainerChecks.cpp`, and `StaticDispatchLowering.cpp`
* **Test Framework:** CppUTest (`CPPUTEST_ROOT`)
* **Build System:** CMake + Make / Ninja (portable `find_package(LLVM REQUIRED CONFIG)`)
* **Version Control:** Git (`main` / `master`, standalone repository with zero external submodules)
* **CLI & Build Guide:** See [`BUILDING.md`](./BUILDING.md) for command line instructions.

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
- [ ] **Task 4.4: External SMT Solver Discharge (`Z3` CLI / Bit-Vector & LIA Contract Verification)**
  - *Current state:* `compiler/src/` emits SMT-LIB2 obligation scripts via `--smt-report` marked `Solver: UNIMPLEMENTED_STUB`, paired with endpoint-check helper `rt_contract_eval_endpoints_stub` in `<host/smt_bridge.h>`.
  - *Deliverable:* Wire real optional/CI `z3 -smt2 -in` process invocation (with deterministic timeout/rlimit) to discharge `@pre`/`@post` verification conditions generated by `--smt-report`, distinguishing `Z3_UNSAT_PROVEN`, `Z3_SAT_COUNTEREXAMPLE` (printing concrete witness assignments in Goose-style diagnostics), and `Z3_UNKNOWN_TIMEOUT`, backed by positive proof tests and negative counterexample tests.

---

## 🔄 Phase 5: End-to-End Integration Verification

- [x] **Task 5.1: Network Processor Kernels & Audio DSP Examples Smoke Suite (`run_examples_smoke_test.sh`)**
- [x] **Task 5.2: Round-Trip `VOBJ` v1 Snapshot Persistence (`order_book_snapshot.c`, `TestTapeAndRelPtr.cpp`)**
- [x] **Task 5.3: Ternary Block Elaboration & Transitive Zero-Multiplication Gate (`ternary_glu_block.m`, `cfg_branch_move_router.m`)**
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

## 🧰 Phase 6: Package Manager & Build Budget Runner (`vpm`) — *Defer Until Tasks 5.5 & 4.4 Complete*
*(Mapped to: `docs/veriobjc-poc-execution-plan.md` §0)*

- [ ] **Task 6.1: `vpm` Bootstrap & Content-Addressed Artifact Store**
  - Standalone CLI tool; pins the Clang/LLVM toolchain and manages a content-addressed store for verified `VOBJ` v1 snapshot images and build manifests (producer-agnostic, zero external repo coupling).
- [ ] **Task 6.2: Package Manifest & Compile-Time Budget Enforcement**
  - Enforces per-package instantiation bytes (Task 2.8.5), `vregex` DFA table bytes, and static arena budgets at build time.
- [ ] **Task 6.3: CI Survivor Report Archival & Diffing**
  - Automates per-commit `--effects-report`, `--branch-report`, and `--mono-report` artifact diffing via `rt_survivor_diff_audit` (Task 4.2).
