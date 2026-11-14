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
| [`sealed_synth_hierarchy.m`](./sealed_synth_hierarchy.m) | **Closed-World Sealed Hierarchies & Static Devirtualization:** Demonstrates `__attribute__((objc_sealed))` base classes (`SynthVoice`) andprotocols (`VoiceModulator`) with `__attribute__((objc_final))` leaf classes (`SawtoothVoice`, `SubBassVoice`). | Every Objective-C message send on the sealed hierarchy is statically devirtualized into a direct C function call with `0` dynamic dispatch symbols. |
| [`linear_channel_router.m`](./linear_channel_router.m) | **Linear Ownership (`move` / `objc_consumed`), Escape Analysis & Flat Channels (`objc_flat`):** Allocates a packet in a generational pool, transfers ownership linearly via `veriobjc_move` into a consuming method, and transmits a pointer-free ` TelemetryEnvelope` over a lock-free SPSC channel (`veriobjc_channel_send`). | Enforces compile-time use-after-move/double-free prevention, lexical stack root escape safety, and recursive flat-struct verification (`objc_flat`). |
| [`simd_range_filter.m`](./simd_range_filter.m) | **Zero-Overhead Range-Based For Loops (`for (T x in container)`):** Iterates over a fixed-capacity `SimdSampleBlock` (`int32_t lanes[16]`) using Objective-C fast-enumeration syntax. | Compiler plugin rewrites `ObjCForCollectionStmt` directly into a raw pointer loop (`__veriobjc_it`, `__veriobjc_end`), eliminating `NSFastEnumeration` and `objc_enumerationMutation` so LLVM auto-vectorizes the loop. |
| [`multiframe_packet_parser.m`](./multiframe_packet_parser.m) | **Zero-Table Multi-Frame Error Returns (`return err from f`):** Deep helper functions (`validate_magic_byte`, `parse_header_layer`) abort directly out of an enclosing frame (`parse_network_frame`) via `veriobjc_return_err_from`. | Plugin statically verifies call-graph containment from every raise site to the named target frame; compiles with `0` DWARF `.eh_frame` tables and `0` unwinder calls (`__cxa_throw` / `objc_exception_throw`). |
| [`non_escaping_block_pipeline.m`](./non_escaping_block_pipeline.m) | **Non-Escaping Function Blocks (`^ret(args){...}`):** Passes a capturing Objective-C block closure (`gain` + `bias`) through a sample pipeline and invokes an inline block literal. | Plugin monomorphizes each block into a `static` C function (`__veriobjc_mono_block_*`) with captured variables threaded as explicit value parameters — `0` `_NSConcreteStackBlock` or `_NSConcreteGlobalBlock` symbols emitted. |
| [`constrained_generic_vault.m`](./constrained_generic_vault.m) | **Definition-Site Checked Constrained Generics (`vgeneric`):** Declares `TelemetryVault<T : TelemetryPayload *>` where the type parameter `T` is constrained to a sealed hierarchy (`TelemetryPayload` / `ImuTelemetry`). | Plugin verifies the sealed constraint bound at the `@interface` definition site and devirtualizes all method calls on `T` without type erasure or `id` boxing. |
| [`vconstexpr_tick_table.m`](./vconstexpr_tick_table.m) | **Fueled Compile-Time Evaluation (`vconstexpr`):** Computes recursive tick-scaling factors (`compute_tick_scale`, ` accumulate_schedule_budget`) at compile time under a strict step/depth fuel budget (`MAX_STEPS = 4096`, `MAX_DEPTH = 64`). | Plugin evaluates `vconstexpr` calls in the AST with memoization and folds them directly into `IntegerLiteral` constants before code generation. |
| [`fix_protocol_dfa_router.m`](./fix_protocol_dfa_router.m) | **Compile-Time Regular Expressions (`vregex`) to `.rodata` DFA:** Matches financial FIX protocol tags (`"8=FIX42\\|35=[A-Z0-9]+\\|"` and `"35=D\\|49=[A-Z]+\\|"`) using `vregex_match`. | Plugin compiles each regex via Thompson NFA -> Powerset Construction -> Hopcroft Minimization into `static const` transition tables in `.rodata` (`__veriobjc_dfa_trans_*`) walked in branchless $O(1)$ time per byte (`veriobjc_dfa_match`). |
| [`ternary_glu_block.m`](./ternary_glu_block.m) | **Hardware Stream Elaboration (`veriStream`) & Zero-DSP Ternary Datapath:** Implements a 512-lane ternary `{−1, 0, +1}` GLU block (`TernaryGLUBlock`) with `vstream_def`, `veriobjc_clog2(512)` latency contract verification (`9` stages), `veriobjc_from_bytes` weight provenance verification, and `veriobjc_ternary_dot_512` CMUX + popcount reduction. | Verified under `--hw-report` to instantiate **0 DSP blocks** (`DSPs: 0 | ReduceStages: 9 | Status: PASS (0 DSPs)`) with zero runtime multiplications. |
| [`static_dag_soa_pipeline.m`](./static_dag_soa_pipeline.m) | **Effect System, FFI `@export`, `@reflect`/SoA, `vvariant`/`vmatch`, `vtrait`, Static Task DAGs (`vasync`) & Built-in-Place Construction:** Showcases Section 2.8 capabilities including `@nolatency`/`@pure`/`@cold` effect verification, `@export` header generation, `vsoa_def3` struct-of-arrays, exhaustive `vmatch`, deduplicated monomorphization, compile-time DAG critical-path WCET folding, stack-allocated `vasync`, and `veriobjc_push_inplace`. | Verified under `--effects-report`, `--mono-report`, and `--export-header-stdout` with `0` dynamic dispatch symbols and `0` heap allocations. |

### B. Freestanding C Micro-Runtime Examples (`examples/*.c`)

| Example File | Demonstrated Runtime Subsystem | Key Verification Guarantee |
| :--- | :--- | :--- |
| [`kernel_dma_batch.c`](./kernel_dma_batch.c) | **Freestanding Arena, Fixed-Block Pool & Generational Pool:** Runs a 64-descriptor network DMA batch using `veriobjc_stack`, `veriobjc_pool`, and `veriobjc_gen_pool` (`handle = {index, generation}`) with $-O(1)$ slot recycling and temporal ABA/use-after-free detection. | Compiles with `-ffreestanding -nostdlib` and `0` undefined external symbols (`nm` shows zero `U` entries). |
| [`order_book_snapshot.c`](./order_book_snapshot.c) | **Zero-Copy Tape Serialization (`to_bytes` / `from_bytes`) & Self-Relative Pointers (`vrelptr<T>`):** Builds a limit order book linked via 32-bit signed self-relative offsets (`veriobjc_relptr32_set`/`get`), serializes it into a position-independent snapshot tape with FNV-1a integrity hashing, and relocates it via byte copy without pointer fixups. | `veriobjc_from_bytes` validates magic, version, alignment, bounds, and FNV-1a checksum in $O(N)$ before returning a typed zero-copy view. |

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

# 3. Or run all 4 test suites together via CTest
ctest --test-dir build --output-on-failure
```

---

### B. Compile Individual Positive Examples Manually

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

#### 2. Compile a Hardware Stream Kernel (`veriStream`) with `--hw-report`
Pass `-Xclang -plugin-arg-veriobjc-check -Xclang --hw-report` to emit the hardware synthesis resource and zero-DSP audit report:
```bash
clang-23 -std=gnu23 -O2 -Wall -Wextra -Werror -Wno-objc-root-class -fblocks \
         -Xclang -load -Xclang build/libVeriObjCPlugin.so \
         -Xclang -add-plugin -Xclang veriobjc-check \
         -Xclang -plugin-arg-veriobjc-check -Xclang --hw-report \
         -I./rt/include \
         -c examples/ternary_glu_block.m \
         -o build/ternary_glu_block.o
```
Expected `--hw-report` output:
```text
[VeriObjC --hw-report] Kernel: TernaryGLUBlock::forward:prev:gateTape:tapeLen:upTape:upTapeLen: | LUTs: 2048 | FFs: 512 | BRAMs: 2 | DSPs: 0 | ReduceStages: 9 | Status: PASS (0 DSPs)
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
