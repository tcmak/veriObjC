# 🗺️ VeriObjC Master Implementation & TDD Execution Plan (`plan.md`)

This living checklist tracks the complete execution of **VeriObjC (Verifiable Objective-C)** across its compiler plugin, freestanding micro-runtime (`rt/`), standard library (`vstd/`), and verification harnesses.

Every implementation step follows strict **Test-Driven Development (TDD: Red ➔ Green ➔ Refactor ➔ Audit)**. Tasks are mapped directly to the specifications in [`docs/`](./docs/).

---

## 🧭 Progress Dashboard

* **Overall Status:** Phases 0–3 Implemented (including `clang::CFG` move/escape lattice & transitive zero-DSP call-graph check); Phases 4–5 Partial / In Progress (Task 4.4 SMT solver is scaffolding; Task 5.5 mixed-size sealed composition open)
* **Compiler Substrate:** Clang / LLVM 23.1.5 (`clang-23`)
* **Test Framework:** CppUTest (`CPPUTEST_ROOT`)
* **Build System:** CMake + Make / Ninja
* **Version Control:** Git (`main` / `master`)
* **CLI & Build Guide:** See [`BUILDING.md`](./BUILDING.md) for command line instructions.

---

## 📦 Phase 0: Toolchain, Build System & Environment Setup

- [x] **Task 0.1: VCS Initialization & Structure**
  - Git repository initialized with appropriate `.gitignore`.
  - Directory structure created: `rt/`, `compiler/`, `vstd/`, `tests/` (`unit_rt/`, `plugin/`, `integration/`).
- [x] **Task 0.2: Unified CMake Configuration**
  - Configured C23 / C++26 with Clang 23.
  - Linked CppUTest static libraries and headers.
  - Test runner executable `unit_rt_tests` configured and running via CTest.

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
  - Polymorphic objects carry strictly `sizeof(void*)` isa pointer header.
- [x] **Task 1.3.2: Link-Time Perfect-Hashed Jump Table Resolver [TDD Cycle 7]**
  - 32-bit FNV-1a integer selector hash resolution (`veriobjc_sel_hash`).
  - Conflict-free dispatch table (`veriobjc_vtable`, `veriobjc_vtable_entry`) placed in `.rodata` with O(1) `(sel_id >> shift) & mask` lookup, selector tag verification, and superclass fallback (`veriobjc_dispatch_lookup`, `veriobjc_object_dispatch`).
- [x] **Task 1.3.3: Scoped Cleanup Hook (`VeriObjC_ScopeRelease` / `VERIOBJC_SCOPED_STACK`) [TDD Cycle 4]**
  - RAII-style `__attribute__((cleanup))` automation resetting stack windows upon block termination.
  - Nested scopes unwind deterministically in LIFO order; disassembles to a single `movq` store instruction.
  - 100% branch and null guard coverage; zero libc symbols in `nm`.

### 1.4 Self-Relative Tape Format
*(Mapped to: `docs/veriobjc-poc-execution-plan.md` Module 2, Task 5)*
- [x] **Task 1.4.1: Self-Relative Link Primitives (`__relative(8|16|32)`) [TDD Cycle 8]**
  - Narrow offset pointer encoding (`veriobjc_rel8`, `veriobjc_rel16`, `veriobjc_rel32`) for position-independent structures across raw `memcpy`.
  - Range overflow checks, self-link sentinel rejection (`delta == 0`), and null safety.
- [x] **Task 1.4.2: Verified Framing & Loading (`to_bytes` / `from_bytes`) [TDD Cycle 9]**
  - Framed snapshot serialization (`veriobjc_tape_header`, `veriobjc_to_bytes`) and zero-copy verified loading (`veriobjc_from_bytes`) with 32-bit FNV-1a checksum and structural invariants.
  - Exhaustive byte-by-byte mutation fuzz sweep and hostile header rejection verified in `TestTapeAndRelPtr.cpp`.
- [x] **Task 1.4.3: Self-Relative Link Bounds & Alignment Verification (`veriobjc_verify_rel32_link`) [TDD Cycle 9]**
  - Bounds-checked and element-alignment-checked verification of loaded `__relative(32)` links before dereference.
  - 100.00% region (313/313), function (31/31), and branch (182/182) coverage verified via `llvm-cov-23`; zero external/libc symbols verified via `nm`.

---

## 🛠️ Phase 2: Clang Frontend Plugin (`compiler/`)
*(Mapped to: `docs/veriobjc-poc-execution-plan.md` Module 1, Tasks 1–16)*

### 2.1 AST Plugin Boilerplate & Registry
- [x] **Task 2.1.1: Clang 23 `PluginASTAction` Infrastructure [TDD Cycle 5]**
  - CMake target for `libVeriObjCPlugin.so` linked against `libclang-cpp` and LLVM 23 headers.
  - Registration under `-plugin veriobjc-check` / `-add-plugin veriobjc-check`.
  - Verified loading into Clang 23 without binary or ABI mismatch; added CTest `plugin_tests` target.

### 2.2 Strict Zero-Bypass Dynamic Feature Ban
- [x] **Task 2.2.1: Ban Generic `id` Type [Negative AST Tests] [TDD Cycle 5]**
  - AST Matchers in `VeriObjCVisitor`: intercepts `VarDecl`, `ParmVarDecl`, `FieldDecl`, and `TypedefNameDecl` using `isObjCIdType()`.
  - Immediate fatal error and compilation halt with custom diagnostic: `"VeriObjC Violation: Banned use of generic 'id' type. Use strict nominal object typing."`.
  - Verified across negative test suite (`test_banned_id.m`, `test_banned_id_field.m`, `test_banned_id_param.m`) and positive tests.
- [x] **Task 2.2.2: Ban `NSObject` Inheritance & Apple Runtime Reflection [TDD Cycle 6]**
  - AST Matcher `VisitObjCInterfaceDecl`: inspects inheritance chains; any inheritance from `NSObject` emits fatal error: `"VeriObjC Violation: Banned inheritance from 'NSObject'. VeriObjC classes are freestanding and must not inherit from the dynamic Apple runtime."`.
  - AST Matcher `VisitCallExpr`: rejects calls to dynamic reflection and swizzling functions (`NSClassFromString`, `class_copyMethodList`, `objc_getClass`, `objc_msgSend`, `method_exchangeImplementations`).
- [x] **Task 2.2.3: Untyped Receiver Ban on `ObjCMessageExpr` [TDD Cycle 6]**
  - AST Matcher `VisitObjCMessageExpr`: validates `E->getReceiverInterface() != nullptr`; dynamic calls to untyped `Class` or dynamic receivers emit fatal error: `"VeriObjC Violation: Dynamic message dispatch requires an explicitly typed receiver."`.
  - Verified across negative tests (`test_banned_nsobject.m`, `test_banned_reflection.m`, `test_banned_untyped_msg.m`) and positive class messaging test.

### 2.3 Nominal Devirtualization & Sealed Dispatch
- [x] **Task 2.3.1: Devirtualization of `@final` / Known Types [TDD Cycle 10 / Task C2.3]**
  - Closed-world selector check in `VisitObjCMessageExpr`: rejects undeclared selectors with fatal diagnostic (`test_banned_unresolved_msg.m`) without mutating `ObjCMessageExpr` in the AST.
  - LLVM IR `PassInfoMixin` lowering pass (`VeriObjCStaticDispatchPass` in `compiler/src/StaticDispatchLowering.cpp`, invoked automatically via `PassBuilderCallbacks` and exported via `-fpass-plugin=`) rewrites monomorphic/`@final` `@objc_msg_lookup` call sites directly to `@_i_<Class>__<Selector>` (or header-only `@__veriobjc_direct_*`) and strips `@.objc_load_function`, `@llvm.global_ctors`, `@__objc_exec_class`, and `.objc_*`/`_OBJC_*` metadata globals (`0` ObjC runtime symbols verified via `nm` and `llvm-objdump -d`).
- [x] **Task 2.3.2: Sealed Class & Protocol Dispatch Matrix [TDD Cycle 11 / Task C2.3]**
  - Registered first-class `__attribute__((objc_sealed))` and `__attribute__((objc_final))` Clang Sema attribute handlers via `ParsedAttrInfoRegistry`, enforcing sealed class inheritance (`test_banned_unsealed_inheritance.m`, `valid_objc_unsealed_inheritance.m`), final class non-subclassability (`test_banned_final_inheritance.m`), sealed protocol conformance (`test_banned_unsealed_protocol.m`), and 16-bit closed-world tag uniqueness.
  - Freestanding 8-byte prefix header (`struct veriobjc_sealed_header`, `veriobjc_alloc_sealed` in `<rt/dispatch.h>`, policed by `CfgDataflowChecks.cpp` via `test_banned_escape_sealed_upcast.m`) + LLVM IR tag-switched dispatch synthesis (`@__veriobjc_dispatch_<Class>__<Selector>`) proven end-to-end without `-lobjc` in `tests/integration/test_e2e_sealed_polymorphism.m` (`e2e_sealed_polymorphism_tests`) and `examples/sealed_synth_hierarchy.m`.

### 2.4 Linear Ownership & Data-Stack State Tracker
- [x] **Task 2.4.1: Move Semantics & Consumed State Tracker [TDD Cycle 12 / Task C2.2]**
  - Path-sensitive forward monotone dataflow worklist over `clang::CFG` (`compiler/src/CfgDataflowChecks.cpp`) tracking `Unreached -> Live/Moved -> MaybeMoved` across `if`/`else` join blocks, local pointer aliases, and loop back-edges via `veriobjc_move()` / `__veriobjc_mark_moved` and `__attribute__((objc_consumed))`.
  - Double-free (`veriobjc_pool_free` / `veriobjc_gen_pool_free`), alias use-after-move, and loop back-edge use-after-move rejected at compile time while branch-symmetric moves are accepted (`test_banned_double_free.m`, `test_banned_use_after_move.m`, `test_banned_use_after_move_alias.m`, `test_banned_use_after_move_loop_backedge.m`, `test_valid_branch_symmetric_move.m`).
- [x] **Task 2.4.2: Reference Root Safety [TDD Cycle 12 / Task C2.2]**
  - `clang::CFG` stack-scope provenance lattice (`StackScoped(depth)`) + interprocedural pointer-forwarding summaries proving data-stack allocations (`veriobjc_stack_alloc` / `veriobjc_alloc_init`) and views (`vstring_view`, `vspan`) never escape via outer variables, globals, struct fields, out-parameters, subobject addresses (`&inner[0]`), return values, or identity/forwarding helper functions (`test_banned_escaping_stack_ref.m`, `test_banned_escape_*.m`, `valid_objc_escape_via_out_param.m`).
- [x] **Task 2.4.3: Flat-Value Channel Enforcement [TDD Cycle 12]**
  - Registered `__attribute__((objc_flat))` Sema attribute and statically verifies payload types of `veriobjc_channel_send` and `objc_flat` structs/typedefs contain zero pointer/reference fields at any nesting depth (`test_banned_nonflat_channel.m`, `test_valid_linear_and_channel.m`, `examples/linear_channel_router.m`).

### 2.5 Syntactic Lowering Passes
- [x] **Task 2.5.1: Range-Based For Loop Lowering [TDD Cycle 13]**
  - Intercepts `ObjCForCollectionStmt` (`for (Elem *x in container)`) in `VeriObjCPlugin.cpp` and lowers directly into a raw C pointer `ForStmt` (`__veriobjc_it`, `__veriobjc_end`, `BinaryOperator <`, `UnaryOperator ++`), eliminating `objc_enumerationMutation` and `objc_msg_lookup` (`test_valid_range_for.m`, `examples/simd_range_filter.m`).
  - Statically rejects `for (... in ...)` over containers without a compile-time bounded array field (`test_banned_unbounded_for_in.m`).
- [x] **Task 2.5.2: Multi-Frame Error Return Lowering (`return err from f`) [TDD Cycle 14]**
  - Static call-graph active-frame verifier (`collectErrorReturnGraph` & `verifyMultiFrameErrorReturns`) ensuring every `veriobjc_return_err_from(err, target_frame)` call site provably terminates inside an active `target_frame` without an exception unwinder (`test_valid_err_from.m`, `examples/multiframe_packet_parser.m`).
  - Rejects uncaught `from` error returns escaping to top-level callers (`test_banned_uncaught_err_from.m`).
- [x] **Task 2.5.3: Non-Escaping Function Blocks [TDD Cycle 15]**
  - Monomorphizes non-escaping `BlockExpr` closures (`^ret(args){...}`) into direct static C functions (`__veriobjc_mono_block_*`) with captured variables threaded as explicit parameters, eliminating `_NSConcreteStackBlock` and `_NSConcreteGlobalBlock` (`test_valid_function_block.m`, `examples/non_escaping_block_pipeline.m`).
  - Statically rejects escaping function blocks in global variables, struct/class fields, assignments, or return values (`test_banned_escaping_block.m`).
- [x] **Task 2.5.4: Constrained Generics (`vgeneric`) Definition-Site Checking [TDD Cycle 16]**
  - Enforces explicit sealed constraint bounds (`<T : SealedBound *>`) on all generic type parameters (`ObjCTypeParamList`) at the definition site and devirtualizes constraint method calls (`test_valid_constrained_generic.m`, `examples/constrained_generic_vault.m`).
  - Rejects unconstrained generic parameters or unsealed constraint bounds (`test_banned_unconstrained_generic.m`).
- [x] **Task 2.5.5: Fueled Interpreter for `vconstexpr` [TDD Cycle 17]**
  - Implemented AST-level fuel-bounded interpreter (`evalVConstexprCall`, `MAX_STEPS = 4096`, `MAX_DEPTH = 64`) with program-wide memoization (`VConstexprMemo`) and constant folding into `IntegerLiteral` (`test_valid_vconstexpr.m`, `examples/vconstexpr_tick_table.m`).
  - Cleanly aborts compilation naming the offending function, steps consumed, and recursion depth when fuel is exhausted (`test_banned_vconstexpr_fuel.m`).
- [x] **Task 2.5.6: Compile-Time Regex (`vregex`) to DFA in `.rodata` [TDD Cycle 18]**
  - Implemented Thompson NFA construction, Powerset Subset Construction, and Hopcroft Equivalence-Class Minimization inside `VeriObjCPlugin.cpp` (`compileRegexToMinimizedDFA`), lowering `vregex_match(pattern, input, len)` into `.rodata` transition tables (`__veriobjc_dfa_trans_*`) and branchless O(1)-per-byte `veriobjc_dfa_match` calls (`test_valid_vregex.m`, `examples/fix_protocol_dfa_router.m`).
  - Statically rejects backreferences (`\1`..`\9`), lookaround assertions (`(?=`, `(?!`), and excessive group nesting (`test_banned_backref_regex.m`).
- [x] **Task 2.5.7: Nominal Local Type Inference (`auto`) [TDD Cycle 19]**
  - Single-pass exact nominal deduction for local `VarDecl`s (`AutoType` resolved to concrete deduced type in AST; array-to-pointer decay rejected under `auto`; `vauto_borrow` yields root-checked borrows; `vstrong` types never unwrap under inference: `test_valid_auto_local.m`).
  - Statically rejects `auto` / `vauto` in function/method parameters (`test_banned_auto_param.m`) and in protocol or `@export` (`veriobjc_export`) method/function return types (`test_banned_auto_export.m`).

### 2.6 Hardware Stream Elaboration (`veriStream`)
*(Mapped to: `docs/veriobjc-advanced-substrate.md` §6 — Fixed-Point Streaming Substrate (`veriStream`))*

- [x] **Task 2.6.1: Wire-Level Stream Types (`Bits<W>` / `Stream<T, N>`) [TDD Cycle 20]**
  - `W` and `N` as compile-time value parameters (`vconstexpr` `veriobjc_clog2` — depends on Task 2.5.5).
  - `Stream<T, N>` (`vstream_def`) type-checked as flat wire-level values: no memory, no addresses, no capacity, no bounds checks.
  - Reject width-mismatched `zip` (`vstream_zip`) and context cap overflow (`vstream_check_context`) at compile time (`test_banned_stream_width_mismatch.m`, `test_banned_context_overflow.m`).
- [x] **Task 2.6.2: `ternary_select` → CMUX Ladder + Popcount Adder Trees [TDD Cycle 20]**
  - Lower `y += (w == 1 ? x : w == -1 ? -x : 0)` (`veriobjc_ternary_select`) to a MUX ladder (`veriobjc_cmux`) — never a branch — with accumulation as popcount reduction (`veriobjc_ternary_popcount_reduce`: `sum = popcount(w=+1 lanes) − popcount(w=−1 lanes)`).
  - Lower `reduceTree` (`veriobjc_ternary_dot_512`) to `log2(N)` registered stages (`vstream_latency_contract`); pipeline depth verified as a compile-time latency contract (`ReduceStages: 9`).
  - Positive/negative tests: `test_valid_ternary_dot.m`, `examples/ternary_glu_block.m`.
- [x] **Task 2.6.3: `objc_hw_kernel` Zero-Multiplication & Transitive Call-Graph Gate [TDD Cycle 20 / Task C2.2]** *(RTL resource estimation is outside `veriObjC` scope)*
  - Statically verify `vstream_latency_contract` (`log2(N)` reduce stages) and `vstream_check_context` bounds on `objc_hw_kernel` functions/methods; **fail the build on both direct and transitive helper runtime multiplication/division (`*`, `*=`, `/`, `/=`, `%`, `%=`) across the call graph** (`test_banned_zero_dsp_helper_mul.m`; hardware LUT/FF/BRAM/DSP resource reporting is not owned by `veriObjC`).

### 2.7 Ban List Compliance Suite
*(Mapped to: `docs/veriobjc-ban-list.md` — Centralized Ban Registry & Valid-ObjC Rejection Corpus)*

- [x] **Task 2.7.1: Centralized Ban Registry & Diagnostic Compliance Tests [TDD Cycle 21]**
  - Single-source ban registry (`docs/veriobjc-ban-list.md`): every banned construct mapped to its exact diagnostic string and test file (`tests/plugin/negative/test_*.m`).
  - Unit tests assert the **named diagnostic fires**, not merely compilation failure — diagnostic text matched per file.
  - CI gate: 100% of the registry's corpus fails under the plugin (`ban_corpus_tests`).
- [x] **Task 2.7.2: Valid-ObjC Rejection Corpus (`examples/banned/`) [TDD Cycle 21]**
  - Files of **perfectly valid stock Objective-C** that VeriObjC rejects (`examples/banned/valid_objc_*.m`): `id` usage, `NSObject` inheritance, untyped messaging, nil messaging, runtime reflection, method swizzling, categories, KVC/KVO, `@dynamic` properties, `performSelector:`, lightweight generics, GCD spawns, `@try`/`@catch`, escaping blocks, and allocator mixing.
  - Dual-direction gate per file (`tests/integration/run_ban_corpus_test.sh`, CTest target `ban_corpus_tests`): compiles `-Werror` clean under **stock Clang 23** *and* fails under the plugin with the exact named diagnostic.
  - Every corpus file pairs with its registry row and the ban's replacement.


### 2.8 Backfill: Effect System, FFI Discipline, Reflection & Topology
*(Mapped to: `docs/veriobjc-advanced-substrate.md` §2 & §4, the vmeta specification, `docs/veriobjc-poc-execution-plan.md` Module 1 Tasks 7/11/13/14/15, and `docs/veriobjc-ban-list.md` — spec-referenced plugin passes with no prior home in this plan. Before checking these off, verify which ones already have green tests from earlier cycles.)*

- [x] **Task 2.8.1: Effect Type System & `@nolatency` Enforcement [TDD Cycle 22]**
  - Effects on signatures (`@alloc`, `@io`, `@blocking`, `@fence`, `@shared-retain`); contexts `@nolatency` / `@pure`; `@cold` as the quarantine tier (`veriobjc_effect_*`, `veriobjc_nolatency`, `veriobjc_pure`, `veriobjc_cold`).
  - Effectful call inside a `@nolatency` or `@pure` loop is a compile error (`test_banned_heap_nolatency.m`, `test_banned_blocking_io.m`); `@shared` refcounting outside `@cold` is a compile error (`test_banned_shared_nolatency.m`, `valid_objc_shared_nolatency.m`).
  - Implements `--effects-report` survivor reporting per function and source line (`test_valid_effect_system.m`).
- [x] **Task 2.8.2: FFI Effect & Boundary Checking [TDD Cycle 22]**
  - `extern fn` declares effects; unannotated defaults to `@cold` and is uncallable from `@nolatency`/`@pure` contexts (`test_cold_ffi_call.m`).
  - Flat values or opaque `vpool` handles (`struct veriobjc_handle`) only across the `@export` boundary (`test_banned_ffi_nonflat_export.m`); exported layouts pinned (`veriobjc_packed`/`veriobjc_aligned` + `vstatic_assert`); allocator mixing rejected (`test_ffi_stack_ref_export.m`, `test_banned_alloc_mixing.m`).
  - `@export` functions generate a C header (`--export-header-stdout`) carrying `@pre`/`@post` as documented preconditions (`test_valid_ffi_export.m`).
- [x] **Task 2.8.3: Compile-Time Reflection & SoA Transmuter [TDD Cycle 23]**
  - `@reflect()` field-count (`veriobjc_reflect_field_count`), `sizeof` (`veriobjc_reflect_sizeof`), and field-offset (`veriobjc_reflect_offset`) computation plus `vsoa_def3` (`__attribute__((objc_soa))`) lowered at compile time to flat, 64-byte-aligned C arrays (`test_valid_reflect_soa.m`, `examples/static_dag_soa_pipeline.m`).
- [x] **Task 2.8.4: `vvariant`/`vmatch` Exhaustiveness & `vtrait` Closed Tables [TDD Cycle 23]**
  - `vmatch` verified for compile-time exhaustiveness over `objc_vvariant` tag enums; missing a case fails compilation naming the `vvariant` and missing case (`test_banned_nonexhaustive_match.m`, `test_valid_vvariant_match.m`).
  - `vtrait` closed trait tables evaluated inside the fueled `vconstexpr` interpreter (`objc_vtrait`).
- [x] **Task 2.8.5: Monomorphization Deduplication & Instantiation Budgets [TDD Cycle 23]**
  - Deduplicates identical non-capturing function blocks and specialized generics program-wide by canonical signature/body key; enforces per-package instantiation budgets (`vmono_instantiate`, `test_banned_mono_budget.m`, `test_valid_mono_dedup.m`) and emits `--mono-report`.
- [x] **Task 2.8.6: Static Topology & Fixed Task DAGs [TDD Cycle 24]**
  - Build-time core affinity and NUMA placement declared in the binary layout (`veriobjc_core_topology_def`); no runtime thread creation.
  - Task DAGs evaluated at compile time (`veriobjc_dag_edge`, `veriobjc_dag_critical_path_wcet`): cycles rejected at compile time (`test_banned_dag_cycle.m`) and critical-path WCET folded to an integer constant (`test_valid_static_dag_vasync.m`).
  - `vasync` futures lowered to compact stack-allocated state structs (`struct veriobjc_vasync_frame`), woken by explicit hardware interrupt hooks (`veriobjc_vasync_wake_irq`).
- [x] **Task 2.8.7: Built-in-Place Construction Verification [TDD Cycle 24]**
  - Destination-slot threading (`veriobjc_push_inplace`) constructs records directly in the target array slot; intermediate copies of `veriobjc_inplace_only` types are rejected at compile time (`test_banned_temp_copy_inplace.m`, `test_valid_inplace_construct.m`).

---

## 📚 Phase 3: Foundational Standard Library (`vstd/`)
*(Mapped to: `docs/veriobjc-foundational-library.md`)*

- [x] **Task 3.1: Text & Slices (`<vstd/string.h>`, `<vstd/span.h>`)**
  - Non-allocating `vstring_view` (`vfixed_string_def`) and contiguous `vspan<T>` (`vspan_def`), with plugin root-lifetime propagation and 100% `llvm-cov` coverage.
- [x] **Task 3.2: Stack Arrays (`<vstd/stack.h>`)**
  - `vstack_array<T>` (`vstack_array_def`) integrated with compiler-assigned data stacks, enforcing single-active-array per data stack at compile time and zero-relocation growth.
- [x] **Task 3.3: Inplace Containers (`<vstd/vector.h>`)**
  - Fixed capacity `vvector<T, N>` with compile-time bounded assertions, direct slot assignment in `vvector_push` (zero compound-literal temporary), and `vvector_push_inplace` destination-slot construction (`__vvector_storage_def` retained solely as deprecated internal bridge with no new public dependents).
  - **Task 3.3b (`vgeneric` `VVector<T>` & Compiler Friction Fixes):** Implemented constrained `vgeneric` Objective-C container `@interface VVector<T : VVectorItem *>` (`initVec`, `push:`, `at:`, `len`, `cap`, and `for (T item in vec)` range-for iteration), filed and resolved compiler frictions `VG-001` through `VG-004` in `docs/veriobjc-vgeneric-frictions.md`, filed open language frictions `VG-005` (value/integer generic parameters) and `VG-006` (flat `@interface` return-by-value), added ban-corpus enforcement for unsealed type arguments (`test_banned_vvector_unsealed_arg.m`, `valid_objc_vvector_unsealed_arg.m`), and verified return-by-value zero-overhead lowering via disassembly (`build_vector_by_value`, `build_stack_array_by_value`).
- [x] **Task 3.3c: Value-Parameter Generics & AST-Checked `VVector(T, N)` Containers (`VG-005` / `VG-006` / Task C2.1b)**
  - Replaced the fragile source-text `vvector<T, N>` rewriter with native C11/C23/ObjC `VVector_decl` / `VVector(T, N)` macros (`annotate("veriobjc_vvector")` + `_Static_assert`) in `vstd/include/vstd/vector.h` and pure AST `ConstantArrayType` inspection in `compiler/src/ContainerChecks.cpp` (`extractVVectorCapacityFromRecord`, `checkInterfaceByValueRecord`), supporting `.h` headers without `@end`, untouched `"vvector<int, 4>"` string literals, and arbitrary capacities `N` by value (`test_valid_vvector_header_and_caps.m`, `test_banned_vvector_macro_overflow8.m`), plus dual-direction ban-corpus enforcement rejecting by-value carriers for non-`@flat @final @sealed` interfaces (`test_banned_nonflat_interface_by_value.m`, `valid_objc_nonflat_interface_by_value.m`).
  - Deleted the deprecated internal `__vvector_storage_def` bridge macro across the entire codebase (`vstd/include/vstd/vector.h`, `examples/vvector_inplace_batch.m`, `tests/plugin/positive/test_valid_vstd_vector.m`, `tests/unit_rt/TestVstdWave2And3.cpp`) so `<vstd/vector.h>` exposes a single unified container surface (`vvector<T, N>` / `VVector<T>`).
- [x] **Task 3.4: Cache-Aligned Maps (`<vstd/map.h>`, `<vstd/hash.h>`)**
  - `vflat_map` (`vflat_map_def`, parallel contiguous binary search with deterministic key ordering) and `vhash_flat` (`vhash_flat_def`, open addressing with compile-time 75% load-factor ceiling enforcement).
- [x] **Task 3.5: Algebraic Data Types (`<vstd/variant.h>`)**
  - `vvariant` (`vvariant_def`) with zero-overhead inline union layout and exhaustive compile-time checked `vmatch`.
- [x] **Task 3.6: Strong Types (`<vstd/strong.h>`)**
  - `vstrong` newtype wrappers (`vstrong_make`, `vstrong_unwrap`, `vstrong_eq`, `vstrong_lt`) with zero implicit conversions and zero size overhead.
- [x] **Task 3.7: Branch-Proven Result (`<vstd/result.h>`)**
  - `vresult<T, E>` (`vresult_def`) flat error unions with plugin-enforced `.is_ok` branch checking before `.value` access.
- [x] **Task 3.8: Concurrency & Linear Guards (`<vstd/channel.h>`, `<vstd/atomic.h>`)**
  - Lock-free SPSC flat-value channels (`vspsc_channel_def`, with plugin single-producer verification) and linear `vatomic<T>` (`vatomic_def` / `vatomic_acquire` with automatic scope-exit release).
- [x] **Task 3.9: Streaming & Ternary Substrate (`<vstd/stream.h>`, `<vstd/ternary.h>`)**
  - Packed ternary weights {-1, 0, 1} as `Bits(2)` codes (`vternary_pack4` / `vternary_unpack_at`), 4 per byte, verified via `vternary_verify_weights_from_bytes` (`veriobjc_from_bytes`) on load.
  - `ll.ml/model/` blocks: token shift (`vstream_token_shift_def`, 1-cycle register-delayed stream), GLU mux gates (`vternary_glu_mux_gate`, 0-cycle combinational), RMSNorm with power-of-two shifts (`vternary_rmsnorm_shift`), KV cache as pre-banked BRAM arena (`vstream_kv_arena_def`) with declared `ContextCap` — overflow is a compile error (`test_banned_context_overflow.m`, `test_banned_kv_arena_overflow.m`).
- [x] **Task 3.10: Freestanding Floor (`sys0/` / `ll.arch/`)**
  - Arch-tuned `sys0_memcpy`/`sys0_memcpy_nt`/`sys0_memset`/`sys0_memcmp` (`<sys0/mem.h>`); per-core boot trampolines and TLS via `GS`/`TPIDR_EL0` (`<sys0/boot.h>`) — no OS interaction.
  - MMU-as-library (`<sys0/mmu.h>`): page tables, huge pages, DMA-coherent regions, and Write-Back vs Write-Combining caching as distinct types; direct `rdtsc`/`CNTVCT_EL0` clock access layers (`<sys0/time.h>`).
- [x] **Task 3.11: Device & Real-Time Non-Blocking I/O (`<vstd/io.h>`, `hw/`)**
  - Blocking POSIX `read`/`write` banned on constrained paths; fixed-size DMA lines registered directly with devices (`vdma_ring_def`, with static capacity enforcement); direct ring-buffered streaming bound into the execution pipeline.
  - Driver-as-library bindings (`<hw/loopback.h>`) plus an in-process loopback simulation transport (`vio_loopback_roundtrip`) so bare-metal network/UART binaries run on developer machines before target flashing.
- [x] **Task 3.12: Lazy Execution Pipelines & `vasync` (`<vstd/execution.h>`)**
  - P2300-style schedulers/senders/receivers (`vsender_just_i32`, `vsender_then_i32`, `vsender_sync_wait_i32`) as an immutable pipeline mapped on the caller's stack frame; wake-up exclusively via hardware interrupt hooks (`vasync_task_def`, `vasync_irq_wake`) — no work-stealing, no polling rings (`test_banned_vasync_poll_ring.m`).
  - Integrated with Task 2.8.6 (`vtask_dag_edge`, `vtask_dag_critical_path_wcet`).
- [x] **Task 3.13: Hardware-Aware SIMD Vectorization (`<vstd/simd.h>`)**
  - Portable `vsimd_i32x4` vector operations matching scalar reference byte-for-byte; raw vendor intrinsics (`_mm_*`, `__builtin_ia32_*`, `vaddq_*`) banned in `@nolatency` user code (`test_banned_raw_simd_intrinsic.m`).
- [x] **Task 3.14: Contracts Library (`<vstd/contracts.h>`)**
  - Pure (`@pure`) `@pre`/`@post` library surface (`vcontract_pre`, `vcontract_post`, `vcontract_in_range_i64`, `vcontract_non_null`, `vcontract_is_pow2_u64`, `vcontract_aligned_ptr`, `vcontract_implies`, `vcontract_ensures_eq_i64`) with postcondition-to-SMT helpers; pairs with Task 4.4.

### Phase 3 Gate: Library Cohesion & Minimal Binaries
*(vstd stays a layered library, not an umbrella: leaf headers stay leaf; a hello world imports nothing.)*

- [x] **Task 3.15: Cohesion & Minimal-Binary Gate**
  - **Hello-world proof (hosted):** `examples/hello_world.m` — zero vstd imports, `extern fn printf` declared `@io @cold`; links against `rt/` alone. Binary size published by `vstd_cohesion_tests`.
  - **Bare-metal variant:** `examples/hello_uart.m` imports only the `<vstd/io.h>` closure (+ `sys0/` boot + `hw/loopback.h`); no other vstd section linked.
  - **Header-closure budget:** each `<vstd/*.h>`, `<sys0/*.h>`, and `<hw/*.h>` compiles standalone against only its declared layer dependencies (`tests/integration/check_vstd_cohesion.sh`, CTest target `vstd_cohesion_tests`), including a negative self-test verifying that cross-layer imports are caught.
  - **Dead-strip assertion:** `--gc-sections` audit verifying that a binary importing only `<vstd/string.h>` contains no container/substrate code (`nm` on section symbols).

---

## 📊 Phase 4: Measurement, Latency CI & Verification ("No Number, No Claim")
*(Mapped to: `docs/veriobjc-poc-execution-plan.md` Module 3)*

- [x] **Task 4.1: Cycle-Accurate Microbenchmark Harness**
  - CPU cycle measurement (`rdtsc` / `CNTVCT_EL0` via `rt_bench_read_cycles` in `<host/bench_harness.h>`) and deterministic p50/p99/p99.9 + teardown cycle extraction (`rt_bench_compute_stats`) benchmarking unified `vvector<T, N>` containers directly (`tests/unit_rt/TestBenchHarness.cpp`).
  - *Amendment 4 (2026-11-18 / Task B2a):* benchmark reporting adopts the M6 methodology (`rt_bench_validate_m6_record`, `rt_bench_format_m6_line`: fixed non-zero `M×N×K` shapes, cited baselines, $-normalization in cents, `kernel_tier=core|grouped|extended`, and `dsp_mode=zero-dsp|dsp-enabled` named on every number) isolated under `host/include/host/` so `rt/` remains strictly freestanding.
- [x] **Task 4.2: Survivor Reports as Build Artifacts**
  - `--effects-report`: per-line survivor report of effectful call sites.
  - `--branch-report`: classification of proven, hinted (`__builtin_expect`), and unproven data-dependent branches in `@nolatency` / `@pure` functions (`compiler/src/VeriObjCPlugin.cpp`).
  - `--mono-report`: monomorphization byte budget and instantiation tracking.
  - Survivor report diff engine (`rt_survivor_diff_audit` in `<host/survivor_report.h>`, verified in `tests/unit_rt/TestSurvivorReport.cpp` with 100.00% `llvm-cov` coverage) rejecting new `@alloc` effects, surviving effect growth, new unproven branches, monomorphization regressions, and non-zero DSP blocks.
- [x] **Task 4.3: Latency Budget Gates**
  - CI budget check (`rt_latency_gate_check` in `<host/latency_gate.h>`, verified in `tests/unit_rt/TestLatencyBudgetGate.cpp` with 100.00% `llvm-cov` coverage): p50/p99/p99.9/teardown regression beyond `noise_band_cycles` fails closed naming the offending benchmark (`bench=<id>`) and commit (`commit=<sha>`).
- [ ] **Task 4.4: Formal Math Proof Integration (SMACK / Z3)** *(scaffolding only — external SMT solver not yet wired)*
  - Current scaffolding: `@pre` and `@post` boundary extraction (`--smt-report` in `compiler/src/VeriObjCPlugin.cpp` emitting `(assert (<pre>))` / `(assert (not (<post>)))` with `Solver: UNIMPLEMENTED_STUB (no external SMT solver invoked)`) and affine interval endpoint stub `rt_contract_eval_endpoints_stub` (`RT_CONTRACT_ENDPOINT_CHECK_PASSED`) in `<host/smt_bridge.h>`, verified in `tests/unit_rt/TestSmtContractBridge.cpp` with 100.00% `llvm-cov` coverage. Real SMACK/Z3 solver discharge remains unimplemented.

---

## 🔄 Phase 5: End-to-End Integration Verification

- [x] **Task 5.1: Network Processor Kernels & Audio DSP Examples Smoke Suite**
  - Automated acceptance runner (`tests/integration/run_examples_smoke_test.sh`, CTest target `examples_smoke_tests`) compiling `examples/audio_pipeline.m`, `examples/sealed_synth_hierarchy.m`, `examples/kernel_dma_batch.c`, and `examples/order_book_snapshot.c` with `-Werror`.
  - Verified disassembly and symbol tables contain **0 calls to `objc_msgSend` / `objc_msg_lookup`** and **0 undefined external symbols** in freestanding kernel mode.
- [x] **Task 5.2: Round-Trip Snapshot Persistence**
  - Replay order book snapshot (`examples/order_book_snapshot.c` and `tests/unit_rt/TestTapeAndRelPtr.cpp`) through `veriobjc_to_bytes` / `veriobjc_from_bytes` with self-relative link verification and fuzz corruption check (pinned as the standalone `VOBJ` v1 container in Task 5.6b).
- [x] **Task 5.3: Ternary Block Elaboration & Transitive Zero-Multiplication Gate**
  - Compile `examples/ternary_glu_block.m` and `examples/cfg_branch_move_router.m` with `veriobjc-check`; assert the plugin's `objc_hw_kernel` call-graph check rejects both direct and transitive helper `*`/`/` operators (`test_banned_zero_dsp_helper_mul.m`), `ternary_dot<512>` verifies `log2(512) = 9` reduce stages (`@latency(cycles = 9)` for `ternary_dot<512>`), and the full `TernaryGLUBlock` (1-cycle register shift + 9-cycle parallel `ternary_dot<512>` + 0-cycle combinational `glu_gate` mux ladder) declares `@latency(cycles = 10)`.
  - Weight provenance: packed ternary weight image loaded and verified via `veriobjc_from_bytes` before any kernel runs (content-addressed artifact from the hosted training tier).
- ~~**Task 5.4: Vendor Synthesis Validation & RTL Equivalence (DROPPED from `veriObjC` in Task B2)**~~
  - *Boundary cut: Duplicate C/RTL co-simulation interpreters and hardware synthesis reporting are permanently dropped from standalone `veriObjC`. `<vstd/ternary.h>` retains `veriobjc_ternary_dot_512` strictly as a pure-CPU round-half-up software fallback.*
- [ ] **Task 5.5: Mixed-Size Sealed Hierarchy Composition Test (Upcast Dispatch + Scope Reset)**
  - *The seam found in external review: every ingredient is tested separately (sealed dispatch, one-store reset, root safety), but never the composition. A claim that isn't tested isn't a claim.*
  - Scenario: multiple concrete subclasses of one sealed hierarchy, **with differing ivar counts and sizes**, allocated within a single `@scoped` block on one data stack and referenced through **superclass-typed pointers**.
  - TDD: red test first — `tests/unit_rt/TestSealedMixedScope.cpp` (CppUTest) plus `examples/sealed_mixed_scope.m` added to the `examples_smoke_tests` gate.
  - Acceptance criteria:
    - Upcast dispatch: every message through a superclass-typed reference resolves via the perfect-hashed matrix + `isa` discrimination — **zero `objc_msgSend` / `objc_msg_lookup`** (`nm` / `llvm-objdump` audit); subclass ivars intact (no slicing).
    - One-store reset under heterogeneity: scope exit restores the stack top to the scope mark in **exactly one `movq`** despite mixed concrete sizes (`llvm-objdump` audit).
    - No escape: storing any upcast (superclass-typed) reference into an outer-scope destination fails with the reference-root diagnostic; all upcast references are dead at scope exit.
    - 100% region/function/branch coverage on the new paths (`llvm-cov`).
- ~~**Task 5.6a: External Weight Tape Format Reader (RELOCATED out of standalone `veriObjC` in Task B2a)**~~
  - *Boundary cut (Task B2a): `veriObjC` maintains zero external repository dependencies or submodules; `VOBJ` v1 (`fixtures/vobj_v1/`) is `veriObjC`'s sole container format.*
- [x] **Task 5.6b: `VOBJ` v1 Single-Table Snapshot Persistence (`rt/`)**
  - Freestanding `veriobjc_to_bytes` / `veriobjc_from_bytes` / `veriobjc_verify_rel32_link` / `veriobjc_tape_validate_v1` framing, FNV-1a-32 checksum, and self-relative `RelPtr` link rules in `rt/include/veriobjc_runtime.h`.
  - Golden vector corpus: committed 8 `VOBJ` v1 fixtures (`2 accept` + `6 reject`) under `fixtures/vobj_v1/{accept,reject}/` plus exhaustive per-byte mutation verification in `tests/unit_rt/TestTapeAndRelPtr.cpp`.
- [x] **Task 5.7: Host Tooling Isolation & Pure-CPU Round-Half-Up Ternary Fallback (`<vstd/ternary.h>`, `host/`)**
  - Isolated host-only benchmarking, latency-gate, survivor-report, and SMT-stub headers under `host/include/host/` so `rt/` compiles cleanly with `-ffreestanding -mno-sse` and zero `float`/`double` types.
  - Removed invented hardware register structs and floor-shifting mock simulators; aligned `veriobjc_ternary_dot_512` and `vternary_rmsnorm_shift` in `<vstd/ternary.h>` to pure-CPU **round-half-up** (`(acc + (1 << (shift - 1))) >> shift` for `shift > 0`), verified in `tests/unit_rt/TestVstdWave2And3.cpp`.
- ~~**Task 5.8: Stand-In Cross-Repo Conformance Runner (REMOVED in Task B2)**~~
  - *Deleted local stand-in cross-repo certification so `veriObjC` remains strictly self-contained.*

---

## 🧰 Phase 6: Package Manager & Manifest (`vpm`)
*(Mapped to: `docs/veriobjc-poc-execution-plan.md` §0 — Implementation Language Decision Record)*

- [ ] **Task 6.1: `vpm` Bootstrap & Content-Addressed Store**
  - Single static Go binary; downloads, verifies, and pins the exact Clang/LLVM toolchain — `vpm` must never depend on the toolchain it provisions.
  - Content-addressed store for artifacts (weight tapes, snapshot images) with manifest-recorded provenance (training run ID, quantization params, hash).
  - Provenance is **producer-agnostic**: any conforming encoder may write snapshot artifacts; the store and loader depend only on the `VOBJ` v1 container format (Task 5.6b), never on an external producer's toolchain.
- [ ] **Task 6.2: Manifest & Budget Enforcement**
  - Manifest-declared budgets actually enforced at build time: instantiation and generated-code bytes (Task 2.8.5), `vregex` table bytes, and generated-data budget.
  - Exceeding any budget fails the build naming the package and the offending item.
- [ ] **Task 6.3: CI Report Publication & Diffing**
  - `--effects-report`, `--branch-report`, and `--mono-report` published as per-build artifacts and diffed against the previous build (supplies the plumbing for Task 4.2's gates).
  - A certified loop that gains an effect, an unproven branch, or an instantiation fails review even when all tests pass.