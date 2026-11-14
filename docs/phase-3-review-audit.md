# 🔍 Phase 3 Review Audit Trifecta (F3)

This document records the verification evidence for **F3 (Local Audit Trifecta on Waves 2–3 & Task 3.3b)** from `phase-3-execution-guide-foundational-standard-library-vstd.md`:
1. **(a) Mutation Spot-Check** on `compiler/src/VeriObjCPlugin.cpp`
2. **(b) `nm` Zero-libc / Zero-`malloc` Audit** on Wave 2–3 compiled objects (including `map.h`, `hash.h`, `vector.h`, `stack.h`, `variant.h`, `channel.h`, `atomic.h`, `stream.h`, `ternary.h`)
3. **(c) `llvm-objdump -d` Disassembly Audit** of `build_vector_by_value`, `build_stack_array_by_value`, `vvector_push`, `vvector_push_inplace`, and `VVector<T : VVectorItem *>`

---

## 1. (a) Mutation Spot-Check (`analyzeScopeRootSafety`)

### Mutation Applied (`compiler/src/VeriObjCPlugin.cpp`)
Temporarily disabled the `analyzeScopeRootSafety(Body, 0, VarDeclDepth, StackRootDepth)` call inside `VeriObjCVisitor::analyzeAndRewriteBody`:

```diff
-        analyzeScopeRootSafety(Body, 0, VarDeclDepth, StackRootDepth);
+        (void)VarDeclDepth; (void)StackRootDepth; // MUTATION SPOT-CHECK: disabled analyzeScopeRootSafety
```

### Observed Test Failures Under Mutation
Running `ctest --test-dir build -R "plugin_tests|ban_corpus_tests" --output-on-failure` immediately failed both suites on the exact root-depth escape tests (`test_banned_escaping_stack_ref.m`, `test_banned_view_escapes_root.m`, and `valid_objc_view_escapes_root.m`):

```text
[RUN] Negative Test: test_banned_escaping_stack_ref.m
[FAIL] Negative Test did NOT fail compilation as expected: test_banned_escaping_stack_ref.m

[RUN] Negative Test: test_banned_view_escapes_root.m
[FAIL] Negative Test did NOT fail compilation as expected: test_banned_view_escapes_root.m

[RUN] Ban Corpus File: valid_objc_view_escapes_root.m
[FAIL] Corpus file valid_objc_view_escapes_root.m was NOT rejected with 'Escaping data-stack reference'!
```

Restoring `analyzeScopeRootSafety` and rebuilding `libVeriObjCPlugin.so` restored 100% pass across all 5 CTest suites.

---

## 2. (b) `nm` Zero-libc / Zero-`malloc` Symbol Audit on Wave 2–3 Objects

All Wave 2–3 positive plugin tests and example translation units were compiled at `-O2` with `libVeriObjCPlugin.so` and inspected via `nm -u` and `nm -C`:

| Object File | Defined Code Symbols (`T`) | Undefined External Function Symbols (`nm -u`) | Heap / libc / `objc_msgSend` Symbols |
| :--- | :--- | :--- | :--- |
| `test_valid_vstd_vector.o` | `build_vector_by_value`, `build_stack_array_by_value`, `run_valid_vvector` | `0` (only ObjC class metadata refs `__objc_class_name_VVectorItem`, `__objc_exec_class`) | **0** (`malloc=0`, `free=0`, `memcpy=0`, `objc_msgSend=0`) |
| `test_valid_vstd_maps.o` | `run_valid_vstd_maps` | **0** (completely empty `nm -u`) | **0** (`malloc=0`, `free=0`, `realloc=0`, `memcpy=0`) |
| `test_valid_vstd_variant.o` | `handle_venue_reply` | **0** (completely empty `nm -u`) | **0** |
| `test_valid_vstd_channel_atomic.o` | `run_valid_channel_and_atomic` | **0** (completely empty `nm -u`) | **0** |
| `test_valid_vstd_stream_ternary.o` | `run_valid_stream_ternary` | **0** (completely empty `nm -u`) | **0** |
| `test_valid_vstd_stack_array.o` | `collect_samples` | **0** (completely empty `nm -u`) | **0** |
| `vvector_inplace_batch.o` | `evaluate_generic_order_batch`, `run_vvector_batch_demo` | `0` (only ObjC class metadata refs) | **0** (`malloc=0`, `free=0`, `memcpy=0`, `objc_msgSend=0`) |
| `vflat_and_hash_lookup.o` | `run_maps_lookup_demo` | **0** (completely empty `nm -u`) | **0** (`malloc=0`, `free=0`, `realloc=0`) |
| `vvariant_order_events.o` | `eval_order_event` | **0** (completely empty `nm -u`) | **0** |
| `vchannel_atomic_telemetry.o` | `run_channel_atomic_demo` | **0** (completely empty `nm -u`) | **0** |
| `vternary_stream_block.o` | `run_ternary_stream_demo` | **0** (completely empty `nm -u`) | **0** |
| `vstack_array_growth.o` | `accumulate_scoped_ticks` | **0** (completely empty `nm -u`) | **0** |

---

## 3. (c) `llvm-objdump -d` Disassembly Audit (`<vstd/vector.h>` & `<vstd/stack.h>`)

### 3.1 `build_vector_by_value`: Return-by-Value + `vvector_push_inplace` + `vvector_push`
Compiled with `clang-23 -O2 -fplugin=build/libVeriObjCPlugin.so -c tests/plugin/positive/test_valid_vstd_vector.m`:

```asm
0000000000000000 <build_vector_by_value>:
       0: 48 89 f8                      movq    %rdi, %rax
       3: 48 85 ff                      testq   %rdi, %rdi
       6: 74 0e                         je      0x16 <build_vector_by_value+0x16>
       8: c7 40 04 04 00 00 00          movl    $0x4, 0x4(%rax)
       f: b9 04 00 00 00                movl    $0x4, %ecx
      14: eb 07                         jmp     0x1d <build_vector_by_value+0x1d>
      16: 8b 48 04                      movl    0x4(%rax), %ecx
      19: 85 c9                         testl   %ecx, %ecx
      1b: 74 20                         je      0x3d <build_vector_by_value+0x3d>
      1d: c7 00 01 00 00 00             movl    $0x1, (%rax)
      23: 8d 14 36                      leal    (%rsi,%rsi), %edx
      26: 8d 14 92                      leal    (%rdx,%rdx,4), %edx
      29: 89 50 08                      movl    %edx, 0x8(%rax)
      2c: 83 f9 02                      cmpl    $0x2, %ecx
      2f: 72 0c                         jb      0x3d <build_vector_by_value+0x3d>
      31: c7 00 02 00 00 00             movl    $0x2, (%rax)
      37: 83 c6 64                      addl    $0x64, %esi
      3a: 89 70 0c                      movl    %esi, 0xc(%rax)
      3d: c3                            retq
```

**Verification Findings:**
- **Zero stack frame (`subq %rsp` = 0):** The returned `IntLotVec4` is constructed directly into the caller's `sret` destination pointer (`%rdi` / `%rax`).
- **`vvector_push_inplace` (`0x1d–0x29`):** Computes `base * 10` in `%edx` and writes directly to `out.buf[0]` at `0x8(%rax)` (`movl %edx, 0x8(%rax)`).
- **`vvector_push` (`0x31–0x3a`):** Computes `base + 100` in `%esi` and writes directly to `out.buf[1]` at `0xc(%rax)` (`movl %esi, 0xc(%rax)`) — **zero compound-literal stack temporary** and **zero byte-copy loop / `memcpy`**.

### 3.2 `build_stack_array_by_value`: Growable `vstack_array<T>` Returned by Value
```asm
0000000000000040 <build_stack_array_by_value>:
      40: 48 89 f8                      movq    %rdi, %rax
...
      48: 48 89 30                      movq    %rsi, (%rax)
...
      50: 48 8b 7e 08                   movq    0x8(%rsi), %rdi
      54: 48 89 78 08                   movq    %rdi, 0x8(%rax)
      58: c7 40 10 00 00 00 00          movl    $0x0, 0x10(%rax)
...
      85: 89 17                         movl    %edx, (%rdi)
      87: ff 40 10                      incl    0x10(%rax)
...
      bb: 89 0a                         movl    %ecx, (%rdx)
...
      ce: 89 48 10                      movl    %ecx, 0x10(%rax)
      d1: c3                            retq
```

**Verification Findings:**
- The 24-byte `vstack_array` descriptor `{ stack, base, count, elem_size }` is initialized in-place at `(%rax)` while elements `a` (`%edx`) and `b` (`%ecx`) are bump-allocated directly on the caller's `veriobjc_data_stack` (`%rsi`) with zero heap allocation and zero copy on return.

### 3.3 `run_valid_vvector`: Constrained `vgeneric` `VVector<OrderLotItem *>` Devirtualization + Range-For Lowering
```asm
00000000000000e0 <run_valid_vvector>:
      e0: 48 85 ff                      testq   %rdi, %rdi
...
      fc: 0f 57 c0                      xorps   %xmm0, %xmm0
      ff: 0f 11 47 18                   movups  %xmm0, 0x18(%rdi)
     103: 48 89 77 08                   movq    %rsi, 0x8(%rdi)
     107: 48 89 57 10                   movq    %rdx, 0x10(%rdi)
     10b: 48 b8 02 00 00 00 04 00 00 00 movabsq $0x400000002, %rax
     115: 48 89 07                      movq    %rax, (%rdi)
     118: 8b 46 04                      movl    0x4(%rsi), %eax
     11b: 8b 4a 04                      movl    0x4(%rdx), %ecx
     11e: 8d 04 41                      leal    (%rcx,%rax,2), %eax
     121: 83 c0 1e                      addl    $0x1e, %eax
     124: c3                            retq
```

**Verification Findings:**
- `[vec initVec]`, `[vec push:first]`, `[vec push:second]`, `[vec at:0u]`, and `for (OrderLotItem *item in vec)` all devirtualize and inline into 15 instructions with **zero `objc_msgSend`**, **zero `objc_enumerationMutation`**, and **zero function calls**.
