# 🗺️ VeriObjC Master Implementation & TDD Execution Plan (`plan.md`)

This living checklist tracks the complete execution of **VeriObjC (Verifiable Objective-C)** across its compiler plugin, freestanding micro-runtime (`rt/`), standard library (`vstd/`), and verification harnesses.

Every implementation step follows strict **Test-Driven Development (TDD: Red ➔ Green ➔ Refactor ➔ Audit)**. Tasks are mapped directly to the specifications in [`docs/`](file:///home/user/veriObjC/docs/).

---

## 🧭 Progress Dashboard

* **Overall Status:** Phase 0, Phase 1 & Phase 2 Complete (100% Region, Function & Branch Coverage on `rt/`; 100% Plugin & Examples Smoke Suites Passing)
* **Compiler Substrate:** Clang / LLVM 23.1.5 (`/usr/bin/clang-23`)
* **Test Framework:** CppUTest (`/home/user/cpputest`)
* **Build System:** CMake + Make / Ninja
* **Version Control:** Git (`main` / `master`)
* **CLI & Build Guide:** See [`BUILDING.md`](file:///home/user/veriObjC/BUILDING.md) for command line instructions.

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
- [x] **Task 1.2.5: Weak Generation-Counted Handle (`vweak<T>`) [TDD Cycle 3]**
  - Implemented `veriobjc_gen_pool` and `veriobjc_handle` (`slot`, `generation`).
  - Active slots hold odd generation; freeing bumps to even generation; reallocating bumps to next odd.
  - Stale handle access provably detects generation mismatch and returns `NULL` (`stale`) instead of reading garbage.
  - Double-free is mathematically rejected via generation state check.
  - 100% branch coverage verified with `llvm-cov`; zero libc/malloc symbols verified with `nm`.

### 1.3 Headerless Object Packing & Link-Time Sealed Dispatch
*(Mapped to: `docs/veriobjc-poc-execution-plan-updated.md` Module 2, Tasks 1 & 3)*
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
*(Mapped to: `docs/veriobjc-poc-execution-plan-updated.md` Module 2, Task 5)*
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
*(Mapped to: `docs/veriobjc-poc-execution-plan-updated.md` Module 1, Tasks 1–16)*

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
- [x] **Task 2.3.1: Devirtualization of `@final` / Known Types [TDD Cycle 10]**
  - Closed-world selector check in `VisitObjCMessageExpr`: rejects undeclared selectors with fatal diagnostic (`test_banned_unresolved_msg.m`).
  - Rewrites `[receiver message]` directly into direct C-function calls (`CallExpr` to `__veriobjc_direct_<Class>__<Selector>`) in `HandleTopLevelDecl` prior to LLVM IR emission, eliminating `objc_msgSend` and `objc_msg_lookup` (`0` dynamic dispatch calls verified via `nm` and `llvm-objdump -d`).
- [x] **Task 2.3.2: Sealed Class & Protocol Dispatch Matrix [TDD Cycle 11]**
  - Registered first-class `__attribute__((objc_sealed))` and `__attribute__((objc_final))` Clang Sema attribute handlers via `ParsedAttrInfoRegistry`.
  - Enforces sealed class inheritance (`test_banned_unsealed_inheritance.m`), final class non-subclassability (`test_banned_final_inheritance.m`), and sealed protocol conformance (`test_banned_unsealed_protocol.m`).

### 2.4 Linear Ownership & Data-Stack State Tracker
- [x] **Task 2.4.1: Move Semantics & Consumed State Tracker [TDD Cycle 12]**
  - Linear ownership graph (`Unallocated -> Active -> Consumed/Dead`) via `veriobjc_move()` / `__veriobjc_mark_moved` and `__attribute__((objc_consumed))` parameter tracking.
  - Double-free (`veriobjc_pool_free` / `veriobjc_gen_pool_free`) and use-after-move compile rejection (`test_banned_double_free.m`, `test_banned_use_after_move.m`).
- [x] **Task 2.4.2: Reference Root Safety [TDD Cycle 12]**
  - Statically proves every data-stack reference (`veriobjc_stack_alloc` / `veriobjc_alloc_init`) is bounded by its declaring lexical scope depth (`analyzeScopeRootSafety`).
  - Rejects escaping data-stack references assigned to outer-scope destinations (`test_banned_escaping_stack_ref.m`).
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
- [ ] **Task 2.5.7: Nominal Local Type Inference (`auto`)**
  - Single-pass exact nominal deduction for local `VarDecl`s: no implicit conversions, no array decay, no proxy capture; `auto &` yields a root-checked borrow (same analyzer as Task 2.4.2); `vstrong` types never unwrap under inference.
  - Reject `auto` in function parameters (`test_banned_auto_param.m`) and in protocol/`@export` method return types (`test_banned_auto_export.m`); private/helper returns permitted.

### 2.6 Hardware Stream Elaboration (`veriStream`)
*(Mapped to: `docs/veriobjc-advanced-substrate-updated.md` §6 — Neural Inference Substrate)*

- [ ] **Task 2.6.1: Wire-Level Stream Types (`Bits<W>` / `Stream<T, N>`)**
  - `W` and `N` as compile-time value parameters (`vconstexpr` — depends on Task 2.5.5).
  - `Stream<T, N>` type-checked as flat wire-level values: no memory, no addresses, no capacity, no bounds checks.
  - Reject width-mismatched `zip` at compile time (`test_banned_stream_width_mismatch.m`).
- [ ] **Task 2.6.2: `ternary_select` → CMUX Ladder + Popcount Adder Trees**
  - Lower `y += (w == 1 ? x : w == -1 ? -x : 0)` to a MUX ladder — never a branch — with accumulation as popcount reduction (`sum = popcount(w=+1 lanes) − popcount(w=−1 lanes)`).
  - Lower `reduceTree` to `log2(N)` registered stages; pipeline depth carried as a type-level latency contract visible to the model-graph scheduler (Fixed Task DAG machinery).
  - Positive/negative tests: `test_valid_ternary_dot.m`, `examples/ternary_glu_block.m`.
- [ ] **Task 2.6.3: Elaboration Budgets & `--hw-report`**
  - Elaborate `map` as N parallel hardware copies counted against a manifest-declared LUT budget (same budget machinery as `--mono-report`).
  - Emit `--hw-report`: LUTs/FFs/BRAMs/DSPs per kernel; **fail the build if the post-synthesis vendor report shows any DSP block instantiated**.

### 2.7 Ban List Compliance Suite
*(Mapped to: `docs/veriobjc-ban-list.md` — Ban Registry & Rejection Corpus)*

- [ ] **Task 2.7.1: Centralized Ban Registry & Diagnostic Compliance Tests**
  - Single-source ban registry (`docs/veriobjc-ban-list.md`): every banned construct mapped to its exact diagnostic string and test file (existing ✓ tests from Tasks 2.2/2.4/2.5 imported as rows; ⬜ rows added as their features land).
  - Unit tests assert the **named diagnostic fires**, not merely compilation failure — diagnostic text matched per file.
  - CI gate: 100% of the registry's corpus fails under the plugin (`ban_corpus_tests`).
- [ ] **Task 2.7.2: Valid-ObjC Rejection Corpus (`examples/banned/`)**
  - Files of **perfectly valid stock Objective-C** that VeriObjC rejects: `id` usage, categories, swizzling, KVC/KVO, nil messaging, `@try`/`@catch`, GCD spawns, lightweight generics, escaping blocks, runtime class lookup, `performSelector:`, `@dynamic` properties.
  - Dual-direction gate per file (`tests/integration/run_ban_corpus_test.sh`): compiles `-Werror` clean under **stock Clang 23** *and* fails under the plugin with the exact named diagnostic. A file that doesn't compile as plain ObjC is rejected from the corpus — it proves nothing.
  - Every corpus file pairs with its registry row and the ban's replacement (id → sealed protocols; categories → overloads; KVC → `@reflect`; nil messaging → `vvariant`; `@try` → `return err from`; GCD → static topology).


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
- [ ] **Task 3.9: Neural Inference Substrate (`<vstd/stream.h>`, `<vstd/ternary.h>`)**
  - Packed ternary weights {-1, 0, 1} as `Bits(2)` codes, 4 per byte, `consteval` tables pinned in BRAM; weight image verified via `from_bytes` on load.
  - `ll.ml/model/` blocks: token shift (register-delayed stream), GLU mux gates, RMSNorm with power-of-two scales (shifts only), KV cache as pre-banked BRAM arena with declared `ContextCap` — overflow is a compile error (`test_banned_context_overflow.m`).

---

## 📊 Phase 4: Measurement, Latency CI & Verification ("No Number, No Claim")
*(Mapped to: `docs/veriobjc-poc-execution-plan-updated.md` Module 3)*

- [ ] **Task 4.1: Cycle-Accurate Microbenchmark Harness**
  - CPU cycle measurement (`rdtsc` / `CNTVCT_EL0`) on isolated cores.
- [ ] **Task 4.2: Survivor Reports as Build Artifacts**
  - `--effects-report`: per-line survivor report of effectful call sites.
  - `--branch-report`: classification of unproven data-dependent branches.
  - `--mono-report`: monomorphization byte budget and instantiation tracking.
  - `--hw-report`: per-kernel fabric resource audit (LUTs/FFs/BRAMs/DSPs) with the zero-DSP synthesis gate.
- [ ] **Task 4.3: Latency Budget Gates**
  - CI budget check: p50/p99 regression beyond noise band fails build.
- [ ] **Task 4.4: Formal Math Proof Integration (SMACK / Z3)**
  - Extraction of `@pre` and `@post` boundary equations into SMT solver assertions.

---

## 🔄 Phase 5: End-to-End Integration Verification

- [x] **Task 5.1: Network Processor Kernels & Audio DSP Examples Smoke Suite**
  - Automated acceptance runner (`tests/integration/run_examples_smoke_test.sh`, CTest target `examples_smoke_tests`) compiling `examples/audio_pipeline.m`, `examples/sealed_synth_hierarchy.m`, `examples/kernel_dma_batch.c`, and `examples/order_book_snapshot.c` with `-Werror`.
  - Verified disassembly and symbol tables contain **0 calls to `objc_msgSend` / `objc_msg_lookup`** and **0 undefined external symbols** in freestanding kernel mode.
- [x] **Task 5.2: Round-Trip Snapshot Persistence**
  - Replay order book snapshot (`examples/order_book_snapshot.c` and `tests/unit_rt/TestTapeAndRelPtr.cpp`) through `veriobjc_to_bytes` / `veriobjc_from_bytes` with self-relative link verification and fuzz corruption check.
- [ ] **Task 5.3: Ternary Block Synthesis & Zero-DSP Gate**
  - Compile `examples/ternary_glu_block.m` through the HLS flow; assert the synthesis report shows **0 DSP blocks** and `ternary_dot<512>` elaborates to exactly `log2(512) = 9` reduce stages with `@latency(cycles = 9)`.
  - Weight provenance: packed ternary weight image loaded and verified via `veriobjc_from_bytes` before any kernel runs (content-addressed artifact from the hosted training tier).
