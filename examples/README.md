# 📚 VeriObjC Examples & Rejection Corpus Guide

This directory contains two complementary sets of programs:

1. **Positive Showcase Examples (`examples/*.m`, `examples/*.c`)** — Freestanding, zero-allocation, closed-world VeriObjC and micro-runtime examples demonstrating how high-level Objective-C ergonomics compile down to deterministic C/machine code with **0 `objc_msgSend` calls**, **0 `_NSConcreteStackBlock` closures**, **0 DWARF unwinding tables**, and **0 `libc`/OS heap dependencies**.
2. **Valid-ObjC Rejection Corpus (`examples/banned/valid_objc_*.m`)** — Idiomatic stock Objective-C files that compile cleanly (`-Werror`) under plain Clang 23, but are **statically rejected** by the VeriObjC compiler plugin (`libVeriObjCPlugin.so`) with a specific diagnostic.

---

## 🚀 1. Positive Showcase Examples (`examples/`)

### A. Compiler-Lowered Objective-C Examples (`examples/*.m`)

| Example File | Demonstrated Language Features & Compiler Rules | Key Verification Guarantee |
| :--- | :--- | :--- |
| [`audio_pipeline.m`](./audio_pipeline.m) | **Freestanding `@owned` Root Classes & Scoped Data Stacks:** Allocates `BiquadFilter` on a lexical data stack (`VERIOBJC_SCOPED_STACK`, `veriobjc_alloc_init`) and processes audio samples without `NSObject` or `malloc`. | `nm` confirms `0` calls to `objc_msgSend` (`[filter processSample:]` is rewritten to `__veriobjc_direct_BiquadFilter__processSample_`); scope exit resets stack pointer in a single store. |
| [`sealed_synth_hierarchy.m`](./sealed_synth_hierarchy.m) | **Closed-World `@sealed` Hierarchies, Polymorphic Base-Pointer Dispatch & Standalone Linking (Task C2.3):** Demonstrates `__attribute__((objc_sealed))` base classes (`AudioNode`) and protocols (`AudioProcessorProtocol`) with mixed-size `__attribute__((objc_final))` leaf classes (`SineOscillator`, `SubBassOscillator`) instantiated in a lexical arena via `veriobjc_alloc_sealed` (`<rt/dispatch.h>`). | Compiles, links **without `-lobjc`**, and runs standalone: `@final` sends lower to direct calls, `AudioNode *` sends lower to 16-bit tag-switched `__veriobjc_dispatch_AudioNode__processNextSample_`, and `nm` confirms `0` `objc_msgSend`, `objc_msg_lookup`, `__objc_exec_class`, or `malloc`/`free` symbols. |
| [`linear_channel_router.m`](./linear_channel_router.m) | **Linear Ownership (`move` / `objc_consumed`), Escape Analysis & Flat Channels (`objc_flat`):** Allocates a packet in a generational pool, transfers ownership linearly via `veriobjc_move` into a consuming method, and transmits a pointer-free ` TelemetryEnvelope` over a lock-free SPSC channel (`veriobjc_channel_send`). | Enforces compile-time use-after-move/double-free prevention, lexical stack root escape safety, and recursive flat-struct verification (`objc_flat`). |
| [`simd_range_filter.m`](./simd_range_filter.m) | **Zero-Overhead Range-Based For Loops (`for (T x in container)`):** Iterates over a fixed-capacity `SimdSampleBlock` (`int32_t lanes[16]`) using Objective-C fast-enumeration syntax. | Compiler plugin rewrites `ObjCForCollectionStmt` directly into a raw pointer loop (`__veriobjc_it`, `__veriobjc_end`), eliminating `NSFastEnumeration` and `objc_enumerationMutation` so LLVM auto-vectorizes the loop. |
| [`multiframe_packet_parser.m`](./multiframe_packet_parser.m) | **Zero-Table Multi-Frame Error Returns (`return err from f`):** Deep helper functions (`validate_magic_byte`, `parse_header_layer`) abort directly out of an enclosing frame (`parse_network_frame`) via `veriobjc_return_err_from`. | Plugin statically verifies call-graph containment from every raise site to the named target frame; compiles with `0` DWARF `.eh_frame` tables and `0` unwinder calls (`__cxa_throw` / `objc_exception_throw`). |
| [`non_escaping_block_pipeline.m`](./non_escaping_block_pipeline.m) | **Non-Escaping Function Blocks (`^ret(args){...}`):** Passes a capturing Objective-C block closure (`gain` + `bias`) through a sample pipeline and invokes an inline block literal. | Plugin monomorphizes each block into a `static` C function (`__veriobjc_mono_block_*`) with captured variables threaded as explicit value parameters — `0` `_NSConcreteStackBlock` or `_NSConcreteGlobalBlock` symbols emitted. |
| [`constrained_generic_vault.m`](./constrained_generic_vault.m) | **Definition-Site Checked Constrained Generics (`vgeneric`):** Declares `TelemetryVault<T : TelemetryPayload *>` where the type parameter `T` is constrained to a sealed hierarchy (`TelemetryPayload` / `ImuTelemetry`). | Plugin verifies the sealed constraint bound at the `@interface` definition site and devirtualizes all method calls on `T` without type erasure or `id` boxing. |
| [`vconstexpr_tick_table.m`](./vconstexpr_tick_table.m) | **Fueled Compile-Time Evaluation (`vconstexpr`):** Computes recursive tick-scaling factors (`compute_tick_scale`, ` accumulate_schedule_budget`) at compile time under a strict step/depth fuel budget (`MAX_STEPS = 4096`, `MAX_DEPTH = 64`). | Plugin evaluates `vconstexpr` calls in the AST with memoization and folds them directly into `IntegerLiteral` constants before code generation. |
| [`fix_protocol_dfa_router.m`](./fix_protocol_dfa_router.m) | **Compile-Time Regular Expressions (`vregex`) to `.rodata` DFA:** Matches financial FIX protocol tags (`"8=FIX42\\|35=[A-Z0-9]+\\|"` and `"35=D\\|49=[A-Z]+\\|"`) using `vregex_match`. | Plugin compiles each regex via Thompson NFA -> Powerset Construction -> Hopcroft Minimization into `static const` transition tables in `.rodata` (`__veriobjc_dfa_trans_*`) walked in branchless $O(1)$ time per byte (`veriobjc_dfa_match`). |
| [`ternary_glu_block.m`](./ternary_glu_block.m) | **Hardware Stream Elaboration (`veriStream`) & Zero-Multiplication Ternary Datapath:** Implements a 512-lane ternary `{−1, 0, +1}` GLU block (`TernaryGLUBlock`) with `vstream_def`, `veriobjc_clog2(512)` latency contract verification (`9` stages), `veriobjc_from_bytes` weight provenance verification, and `veriobjc_ternary_dot_512` CMUX + popcount reduction. | Verified under `veriobjc-check` (`objc_hw_kernel`) to contain zero direct runtime multiplications and zero dynamic dispatch symbols. |
| [`static_dag_soa_pipeline.m`](./static_dag_soa_pipeline.m) | **Effect System, FFI `@export`, `@reflect`/SoA, `vvariant`/`vmatch`, `vtrait`, Static Task DAGs (`vasync`) & Built-in-Place Construction:** Showcases Section 2.8 capabilities including `@nolatency`/`@pure`/`@cold` effect verification, `@export` header generation, `vsoa_def3` struct-of-arrays, exhaustive `vmatch`, deduplicated monomorphization, compile-time DAG critical-path WCET folding, stack-allocated `vasync`, and `veriobjc_push_inplace`. | Verified under `--effects-report`, `--mono-report`, and `--export-header-stdout` with `0` dynamic dispatch symbols and `0` heap allocations. |
| [`vstring_view_intro.m`](./vstring_view_intro.m) | **Task 3.1 (`<vstd/string.h>`, `<vstd/span.h>`):** Zero-copy `vstring_view`, bounded `vfixed_string_def`, and contiguous DMA slice `vspan_def` (`vspan_from_array`, `vspan_at`). | `clang-23 -I./rt/include -I./vstd/include -c examples/vstring_view_intro.m` — `0` allocations, `0` dynamic symbols. |
| [`vstrong_order_types.m`](./vstrong_order_types.m) | **Task 3.6 (`<vstd/strong.h>`):** Nominal `vstrong_def` newtypes (`TickPrice`, `LotSize`, `SequenceId`) with explicit construction/unwrap and `auto` preservation. | `clang-23 -I./rt/include -I./vstd/include -c examples/vstrong_order_types.m` — `0` implicit conversions. |
| [`vresult_checked_parse.m`](./vresult_checked_parse.m) | **Task 3.7 (`<vstd/result.h>`):** Branch-proven `vresult_def` error unions (`vresult_ok`, `vresult_err`) with static `.is_ok` guard verification before `.value` access. | `clang-23 -I./rt/include -I./vstd/include -c examples/vresult_checked_parse.m` — `0` stack unwinding tables. |
| [`vstack_array_growth.m`](./vstack_array_growth.m) | **Task 3.2 (`<vstd/stack.h>`):** Grow-only `vstack_array_def` over a scoped data stack with stable element pointers and single-store scope reset. | `clang-23 -I./rt/include -I./vstd/include -c examples/vstack_array_growth.m` — single-resizable-per-stack verified. |
| [`vvector_inplace_batch.m`](./vvector_inplace_batch.m) | **Task 3.3, 3.3b & C2.1b (`<vstd/vector.h>`):** AST-checked `VVector_decl` / `VVector(T, N)` C11 macro containers (supporting arbitrary capacities `N` by value without source-text rewriting) alongside constrained `vgeneric` `VVector<T : VVectorItem *>`, direct slot assignment in `vvector_push`, and `vvector_push_inplace` slot construction. | `clang-23 -I./rt/include -I./vstd/include -c examples/vvector_inplace_batch.m` — `0` heap allocations, `0` intermediate copies. |
| [`vflat_and_hash_lookup.m`](./vflat_and_hash_lookup.m) | **Task 3.4 (`<vstd/map.h>`, `<vstd/hash.h>`):** Cache-aligned sorted `vflat_map_def` (binary search) and open-addressing `vhash_flat_def` (power-of-two probing with 75% load ceiling). | `clang-23 -I./rt/include -I./vstd/include -c examples/vflat_and_hash_lookup.m` — `0` pointer chasing, `0` rehash allocations. |
| [`vvariant_order_events.m`](./vvariant_order_events.m) | **Task 3.5 (`<vstd/variant.h>`):** Closed algebraic sum type `vvariant_def` with exhaustive compile-time checked `vmatch` jump-table dispatch. | `clang-23 -I./rt/include -I./vstd/include -c examples/vvariant_order_events.m` — exact tag + max union layout. |
| [`vchannel_atomic_telemetry.m`](./vchannel_atomic_telemetry.m) | **Task 3.8 (`<vstd/channel.h>`, `<vstd/atomic.h>`):** Lock-free SPSC flat-value ring channel (`vspsc_channel_def`) and scoped linear atomic guard (`vatomic_def` / `vatomic_acquire`). | `clang-23 -I./rt/include -I./vstd/include -c examples/vchannel_atomic_telemetry.m` — SPSC single-producer and scoped release verified. |
| [`vternary_stream_block.m`](./vternary_stream_block.m) | **Task 3.9 (`<vstd/stream.h>`, `<vstd/ternary.h>`):** Packed 2-bit ternary weights verified via `vternary_verify_weights_from_bytes`, token shift delay (`vstream_token_shift_def`), GLU mux gate, round-half-up shift RMSNorm, and pre-banked KV arena (`vstream_kv_arena_def`). | `clang-23 -I./rt/include -I./vstd/include -c examples/vternary_stream_block.m` — `0` multipliers, `ContextCap` verified. |
| [`vio_loopback_ring.m`](./vio_loopback_ring.m) | **Task 3.11 (`<vstd/io.h>`):** Non-blocking DMA descriptor ring (`vdma_ring_def`) and in-process loopback roundtrip (`vio_loopback_roundtrip`). | `clang-23 -I./rt/include -I./vstd/include -c examples/vio_loopback_ring.m` — `0` blocking POSIX I/O calls, ring capacity verified. |
| [`vexecution_irq_pipeline.m`](./vexecution_irq_pipeline.m) | **Task 3.12 (`<vstd/execution.h>`):** P2300-style stack sender pipeline (`vsender_just_i32`, `vsender_then_i32`) and interrupt-woken `vasync` future (`vasync_task_def`, `vasync_irq_wake`). | `clang-23 -I./rt/include -I./vstd/include -c examples/vexecution_irq_pipeline.m` — `0` polling rings, static DAG WCET verified. |
| [`vsimd_packet_checksum.m`](./vsimd_packet_checksum.m) | **Task 3.13 (`<vstd/simd.h>`):** Hardware-aware portable SIMD vectorization (`vsimd_i32x4`) on `@nolatency` paths without raw vendor intrinsics. | `clang-23 -I./rt/include -I./vstd/include -c examples/vsimd_packet_checksum.m` — byte-exact match with scalar reference. |
| [`vcontracts_bounds_proof.m`](./vcontracts_bounds_proof.m) | **Task 3.14 (`<vstd/contracts.h>`):** Pure (`@pure`) contract predicates (`vcontract_in_range_i64`) and `@pre`/`@post` SMT boundary extraction. | `clang-23 -I./rt/include -I./vstd/include -c examples/vcontracts_bounds_proof.m` — `0` surviving effects (`--effects-report`). |
| [`hello_world.m`](./hello_world.m) | **Task 3.15 Hosted Minimal-Binary Proof:** Zero `<vstd/*.h>` imports; `extern fn printf` declared `@io @cold`; links against `rt/` alone. | `clang-23 -I./rt/include -c examples/hello_world.m` — `0` `vstd` headers included (`clang -H`). |
| [`hello_uart.m`](./hello_uart.m) | **Task 3.15 Bare-Metal Minimal-Binary Proof (`<vstd/io.h>`, `<sys0/boot.h>`, `<hw/loopback.h>`):** Imports only the I/O + boot + hardware loopback closure with zero container or math headers linked. | `clang-23 -I./rt/include -I./vstd/include -I./sys0/include -I./hw/include -c examples/hello_uart.m` — minimal closure verified. |
| [`cfg_branch_move_router.m`](./cfg_branch_move_router.m) | **Task C2.2 (`clang::CFG` Move/Escape Lattice & Transitive Zero-DSP Call-Graph Check):** Branch-symmetric `veriobjc_move` across `if`/`else` without post-join use, scoped `&scratch[0]` subobject addressing within lifetime, and multiplier-free `objc_hw_kernel` helper call chains. | `clang-23 -I./rt/include -c examples/cfg_branch_move_router.m` — `0` heap allocations, `0` multipliers across helper call graph. |
| [`sealed_mixed_scope.m`](./sealed_mixed_scope.m) | **Task 5.5 (`<rt/dispatch.h>`, `<vstd/span.h>`): Mixed-Size `@sealed` Hierarchy + `vspan` Superclass-Pointer Batch + One-Store Scope Reset:** Allocates three concrete `@sealed @final` subclasses with differing ivar sizes (`8B`, `16B`, `32B`) on one `VERIOBJC_SCOPED_STACK`, stores them in a `vspan` of `PacketStageNode *` base pointers, and dispatches overridden methods across the heterogeneous slice. | Links standalone **without `-lobjc`** and runs end-to-end (`0` `objc_msgSend` / `__objc_exec_class` / `malloc` symbols; single-store `movq` scope reset). |

### B. Freestanding C Micro-Runtime & `sys0/` Examples (`examples/*.c`)

| Example File | Demonstrated Runtime Subsystem | Key Verification Guarantee |
| :--- | :--- | :--- |
| [`kernel_dma_batch.c`](./kernel_dma_batch.c) | **Freestanding Arena, Fixed-Block Pool & Generational Pool:** Runs a 64-descriptor network DMA batch using `veriobjc_stack`, `veriobjc_pool`, and `veriobjc_gen_pool` (`handle = {index, generation}`) with $-O(1)$ slot recycling and temporal ABA/use-after-free detection. | Compiles with `-ffreestanding -nostdlib` and `0` undefined external symbols (`nm` shows zero `U` entries). |
| [`order_book_snapshot.c`](./order_book_snapshot.c) | **Zero-Copy Tape Serialization (`to_bytes` / `from_bytes`) & Self-Relative Pointers (`vrelptr<T>`):** Builds a limit order book linked via 32-bit signed self-relative offsets (`veriobjc_relptr32_set`/`get`), serializes it into a position-independent snapshot tape with FNV-1a integrity hashing, and relocates it via byte copy without pointer fixups. | `veriobjc_from_bytes` validates magic, version, alignment, bounds, and FNV-1a checksum in $O(N)$ before returning a typed zero-copy view. |
| [`sys0_freestanding_floor.c`](./sys0_freestanding_floor.c) | **Task 3.10 (`<sys0/mem.h>`, `<sys0/time.h>`, `<sys0/mmu.h>`, `<sys0/boot.h>`):** Non-temporal cache-bypass stores (`sys0_memcpy_nt`), hardware cycle counter (`sys0_rdtsc`, `sys0_lfence`), distinct WB/WC/DMA MMU region types, and per-core boot TLS descriptors. | `clang-23 -ffreestanding -nostdlib -I./rt/include -I./sys0/include -c examples/sys0_freestanding_floor.c` — `0` undefined external symbols (`nm`). |

---

## 🚫 2. Valid-ObjC Rejection Corpus (`examples/banned/`)

Every file in `examples/banned/` is **100% valid, idiomatic stock Objective-C** (compiling `-Werror` clean under plain `clang-23`), paired 1-to-1 with a row in [`docs/veriobjc-ban-list.md`](../docs/veriobjc-ban-list.md) that VeriObjC refuses at compile time:

| Corpus File | Banned Objective-C Construct | Expected VeriObjC Plugin Diagnostic Key Phrase | VeriObjC Replacement |
| :--- | :--- | :--- | :--- |
| [`valid_objc_id_usage.m`](./banned/valid_objc_id_usage.m) | Untyped generic `id` object pointer | `"Banned use of generic 'id' type"` | Concrete nominal types or sealed protocols |
| [`valid_objc_nsobject_inheritance.m`](./banned/valid_objc_nsobject_inheritance.m) | Inheriting from `NSObject` | `"Banned inheritance from 'NSObject'"` | Freestanding `@owned` root classes |
| [`valid_objc_untyped_messaging.m`](./banned/valid_objc_untyped_messaging.m) | Messaging an untyped `Class` / `id` receiver | `"Dynamic message dispatch requires an explicitly typed receiver"` | Closed-world nominal devirtualization |
| [`valid_objc_nil_messaging.m`](./banned/valid_objc_nil_messaging.m) | Silent nil-messaging on a `_Nullable` pointer | `"Banned nil messaging on unproven pointer"` | `vvariant` for optional absence; proven non-null pointers |
| [`valid_objc_runtime_reflection.m`](./banned/valid_objc_runtime_reflection.m) | `objc_getClass` / `NSClassFromString` reflection | `"Banned Apple runtime reflection"` | Compile-time `@reflect()` structural metadata |
| [`valid_objc_category_swizzle.m`](./banned/valid_objc_category_swizzle.m) | `method_exchangeImplementations` swizzling | `"Banned runtime method surgery"` | Immutable link-time hashed dispatch tables |
| [`valid_objc_category.m`](./banned/valid_objc_category.m) | Open-world Objective-C categories (`@interface C (Cat)`) | `"Banned categories"` | Static method overloads and sealed protocols |
| [`valid_objc_kvc_roundtrip.m`](./banned/valid_objc_kvc_roundtrip.m) | Key-Value Coding (`setValue:forKey:`) | `"Banned key-value coding/observation"` | Compile-time `@reflect` field offsets |
| [`valid_objc_dynamic_property.m`](./banned/valid_objc_dynamic_property.m) | `@dynamic` runtime-synthesized properties | `"Banned dynamic property synthesis"` | Statically checked, packed `@owned` ivars |
| [`valid_objc_perform_selector.m`](./banned/valid_objc_perform_selector.m) | Deferred selector dispatch (`performSelector:`) | `"Banned deferred dynamic dispatch"` | Direct calls or static task DAG |
| [`valid_objc_lightweight_generics.m`](./banned/valid_objc_lightweight_generics.m) | Type-erased lightweight generics (`NSArray<NSString *>`) | `"Banned erased generic hints"` | Monomorphized constrained `vgeneric` |
| [`valid_objc_gcd_pipeline.m`](./banned/valid_objc_gcd_pipeline.m) | Runtime thread/queue spawning (`dispatch_async`) | `"Banned runtime scheduling on constrained paths"` | Static core topology and fixed `vasync` DAGs |
| [`valid_objc_try_catch.m`](./banned/valid_objc_try_catch.m) | DWARF exception unwinding (`@try` / `@catch`) | `"Banned exception unwinding"` | Zero-table `veriobjc_return_err_from` |
| [`valid_objc_escaping_block.m`](./banned/valid_objc_escaping_block.m) | Returning/storing an escaping block closure | `"Banned escaping function block"` | Non-escaping blocks monomorphized at call site |
| [`valid_objc_alloc_mixing.m`](./banned/valid_objc_alloc_mixing.m) | Mixing C heap `free()` with VeriObjC memory | `"Banned allocator mixing"` | Scoped data stacks and generational `vpool` handles |
| [`valid_objc_blocking_io.m`](./banned/valid_objc_blocking_io.m) | Blocking POSIX `read()` in a `@nolatency` loop | `"Banned blocking I/O in @nolatency context"` | Non-blocking DMA lines and ring-buffered `vasync` |
| [`valid_objc_shared_nolatency.m`](./banned/valid_objc_shared_nolatency.m) | Retaining `@shared` refcounted object outside `@cold` | `"@shared retain permitted only in @cold"` | Scoped data stacks, `vpool`, or `vweak` handles |
| [`valid_objc_view_escapes_root.m`](./banned/valid_objc_view_escapes_root.m) | Task 3.1: `vstring_view` escaping its inner `@scoped` data-stack root | `"Escaping data-stack reference"` | Keep views within their root scope or copy into a `vpool` slot |
| [`valid_objc_unchecked_vresult.m`](./banned/valid_objc_unchecked_vresult.m) | Task 3.7: Reading `vresult.value` without proving `.is_ok` in a branch | `"Unchecked vresult access"` | Guard `.value` access inside `if (res.is_ok)` |
| [`valid_objc_two_stack_arrays.m`](./banned/valid_objc_two_stack_arrays.m) | Task 3.2: Two live growable `vstack_array` instances on one data stack | `"Second live vstack_array"` | Use separate compiler-assigned data stacks per active growable array |
| [`valid_objc_vvector_overflow.m`](./banned/valid_objc_vvector_overflow.m) | Task 3.3: Pushing elements beyond a `VVector<T>`'s static capacity `N` | `"vvector push exceeds static capacity"` | Size `VVector<T>` for the static worst-case bound |
| [`valid_objc_vvector_unsealed_arg.m`](./banned/valid_objc_vvector_unsealed_arg.m) | Task 3.3b: Specializing `VVector<T>` with an unsealed type argument | `"Invalid vgeneric type argument"` | Mark concrete `VVectorItem` subclasses `__attribute__((objc_sealed))` |
| [`valid_objc_vhash_load_ceiling.m`](./banned/valid_objc_vhash_load_ceiling.m) | Task 3.4: Inserting beyond `vhash_flat`'s 75% static load-factor ceiling | `"vhash_flat load-factor ceiling exceeded"` | Size `vhash_flat_def` power-of-two capacity so load <= 75% |
| [`valid_objc_vvariant_added_case.m`](./banned/valid_objc_vvariant_added_case.m) | Task 3.5: Non-exhaustive `vmatch` after adding a new case to a `vvariant` enum | `"Non-exhaustive vmatch on vvariant"` | Explicitly handle every enum case in `vmatch` |
| [`valid_objc_spsc_two_producers.m`](./banned/valid_objc_spsc_two_producers.m) | Task 3.8: Two distinct producer functions writing to one SPSC channel | `"SPSC invariant violated on channel"` | Dedicate one SPSC channel per producer-consumer pair |
| [`valid_objc_unacquired_vatomic.m`](./banned/valid_objc_unacquired_vatomic.m) | Task 3.8: Accessing `vatomic` payload directly without `vatomic_acquire` | `"Direct vatomic payload access without linear acquire guard"` | Acquire a scoped linear guard via `vatomic_acquire` |
| [`valid_objc_kv_arena_overflow.m`](./banned/valid_objc_kv_arena_overflow.m) | Task 3.9: Appending to a KV cache arena at a static index `>= ContextCap` | `"Context cap exceeded — compile error"` | Bound token indices strictly below `ContextCap` |
| [`valid_objc_dma_ring_overflow.m`](./banned/valid_objc_dma_ring_overflow.m) | Task 3.11: Submitting more frames than a DMA ring's static capacity | `"DMA ring capacity exceeded"` | Size `vdma_ring_def` capacity for the static burst bound or poll between bursts |
| [`valid_objc_vasync_poll_ring.m`](./banned/valid_objc_vasync_poll_ring.m) | Task 3.12: Busy-wait polling loop on `vasync_task_is_ready` | `"Banned polling ring in vasync pipeline"` | Wake `vasync` tasks exclusively via hardware interrupt hooks (`vasync_irq_wake`) |
| [`valid_objc_raw_simd_intrinsic.m`](./banned/valid_objc_raw_simd_intrinsic.m) | Task 3.13: Calling raw vendor SIMD intrinsics (`_mm_*`) in `@nolatency` code | `"Raw SIMD intrinsic in @nolatency code is banned"` | Use portable `<vstd/simd.h>` operations (`vsimd_i32x4`) |
| [`valid_objc_escape_via_out_param.m`](./banned/valid_objc_escape_via_out_param.m) | Task C2.2: Storing a `@scoped` data-stack allocation through an out-parameter pointer (`*out_slot = scratch`) | `"Escaping data-stack reference"` | Copy flat values out or allocate long-lived slots in an outer `vpool` |
| [`valid_objc_unsealed_inheritance.m`](./banned/valid_objc_unsealed_inheritance.m) | Task C2.3: Open-world subclassing of a base `@interface` without `__attribute__((objc_sealed))` | `"Polymorphic class"` | Mark polymorphic base and intermediate classes `__attribute__((objc_sealed))` and leaf classes `__attribute__((objc_final))` |
| [`valid_objc_sealed_upcast_escape.m`](./banned/valid_objc_sealed_upcast_escape.m) | Task 5.5: Escaping mixed-size `@sealed` superclass pointers out of an inner `VERIOBJC_SCOPED_STACK` via `vspan_init` | `"Escaping data-stack reference"` | Keep `vspan` slices of `@sealed` arena objects within their lexical `VERIOBJC_SCOPED_STACK` lifetime |

---

## 🛠️ 3. How to Build, Inspect, and Run the Examples

### Prerequisite: Build the Toolchain Plugin
From the repository root, build `build/libVeriObjCPlugin.so` and the unit test binary:
```bash
cmake -B build
cmake --build build -j$(nproc)
```

---

### A. Run the Automated Example & Ban Corpus Suites
You can run all positive examples and the dual-direction ban corpus via CTest or directly via their runner scripts:

```bash
# 1. Run all positive examples (compiles every .m and .c example and audits symbols via nm)
./tests/integration/run_examples_smoke_test.sh build/libVeriObjCPlugin.so .

# 2. Run the dual-direction Ban Corpus suite (verifies stock Clang 23 PASS + VeriObjC Plugin FAIL)
./tests/integration/run_ban_corpus_test.sh build/libVeriObjCPlugin.so .

# 3. Or run all test suites together via CTest
ctest --test-dir build --output-on-failure
```

---

### B. Compile & Run Individual Positive Examples Manually

#### 0. Compile, Link Standalone (Zero `-lobjc`), and Run `sealed_synth_hierarchy.m`
```bash
clang-23 -std=gnu23 -O2 -Wall -Wextra -Werror -Wno-objc-root-class -fblocks \
         -Xclang -load -Xclang build/libVeriObjCPlugin.so \
         -Xclang -add-plugin -Xclang veriobjc-check \
         -fpass-plugin=build/libVeriObjCPlugin.so \
         -I./rt/include \
         examples/sealed_synth_hierarchy.m \
         -o build/sealed_synth_hierarchy

./build/sealed_synth_hierarchy
nm build/sealed_synth_hierarchy | grep -E "objc_msgSend|objc_msg_lookup|__objc_exec_class" || echo "0 ObjC runtime symbols"
```

#### 1. Compile an Objective-C Example with the VeriObjC Plugin
```bash
clang-23 -std=gnu23 -O2 -Wall -Wextra -Werror -Wno-objc-root-class -fblocks \
         -Xclang -load -Xclang build/libVeriObjCPlugin.so \
         -Xclang -add-plugin -Xclang veriobjc-check \
         -I./rt/include \
         -c examples/simd_range_filter.m \
         -o build/simd_range_filter.o
```

Audit the emitted object file with `nm` to verify **0 dynamic Objective-C or block runtime symbols**:
```bash
nm build/simd_range_filter.o
```

#### 2. Compile a Hardware Stream Kernel (`veriStream`) with `veriobjc-check`
Compile `ternary_glu_block.m` with `veriobjc-check` to enforce `objc_hw_kernel` zero-multiplication checking, `vstream_latency_contract`, and `vstream_check_context` at compile time:
```bash
clang-23 -std=gnu23 -O2 -Wall -Wextra -Werror -Wno-objc-root-class -fblocks \
         -Xclang -load -Xclang build/libVeriObjCPlugin.so \
         -Xclang -add-plugin -Xclang veriobjc-check \
         -I./rt/include -I./vstd/include \
         -c examples/ternary_glu_block.m \
         -o build/ternary_glu_block.o
```

#### 3. Inspect `.rodata` DFA Tables on the Regex Router Example
```bash
clang-23 -std=gnu23 -O2 -Wall -Wextra -Werror -Wno-objc-root-class -fblocks \
         -Xclang -load -Xclang build/libVeriObjCPlugin.so \
         -Xclang -add-plugin -Xclang veriobjc-check \
         -I./rt/include \
         -c examples/fix_protocol_dfa_router.m \
         -o build/fix_protocol_dfa_router.o

nm build/fix_protocol_dfa_router.o | grep "__veriobjc_dfa_trans_"
```

#### 4. Compile Freestanding C Micro-Runtime Examples (Zero OS / Zero Libc)
```bash
clang-23 -std=gnu23 -O2 -Wall -Wextra -Werror -ffreestanding -nostdlib \
         -I./rt/include \
         -c examples/kernel_dma_batch.c \
         -o build/kernel_dma_batch.o

# Confirm 0 undefined external symbols:
nm build/kernel_dma_batch.o | grep "U " || echo "0 undefined symbols (100% freestanding)"
```

---

### C. Test a Banned Corpus File in Both Directions

Every file in `examples/banned/valid_objc_*.m` can be tested manually to see the contrast between stock Objective-C and VeriObjC:

```bash
# Direction 1: Stock Clang 23 compiles cleanly with 0 warnings/errors
clang-23 -std=gnu23 -fblocks -fobjc-exceptions -Werror -fsyntax-only \
         examples/banned/valid_objc_nil_messaging.m

# Direction 2: VeriObjC Plugin rejects the file with a deterministic diagnostic
clang-23 -std=gnu23 -fblocks -fobjc-exceptions \
         -Xclang -load -Xclang build/libVeriObjCPlugin.so \
         -Xclang -add-plugin -Xclang veriobjc-check \
         -fsyntax-only examples/banned/valid_objc_nil_messaging.m
```
Expected output from Direction 2:
```text
examples/banned/valid_objc_nil_messaging.m:11:12: error: VeriObjC Violation: Banned nil messaging on unproven pointer; prove non-null or use vvariant.
   11 |     return [probe readCounter];
      |            ^~~~~~~~~~~~~~~~~~~
1 error generated.
```
