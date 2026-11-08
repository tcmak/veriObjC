# 🚫 VeriObjC Ban Registry & Valid-ObjC Rejection Corpus

Two artifacts, both CI gates:

1. **The Ban Registry** — the single source of truth for every construct VeriObjC rejects, each mapped to its exact diagnostic and enforcement test.
2. **The Rejection Corpus** — files of *perfectly valid stock Objective-C* that VeriObjC must refuse to compile.

**The principle: a ban that isn't tested isn't a ban.** And a negative test that doesn't compile under stock Clang proves nothing about Objective-C compatibility — so every corpus file is dual-direction gated: clean under plain Clang 23, rejected under the plugin.

---

## 📛 1. Ban Registry

### A. Dynamic Runtime Features

| Banned construct | Valid ObjC? | Diagnostic (key phrase) | VeriObjC replacement | Test |
| :--- | :--- | :--- | :--- | :--- |
| `id` in any declaration | Yes | "Banned use of generic 'id' type" | Sealed protocols, concrete nominal types | `test_banned_id.m` (+ field/param) ✓ |
| `NSObject` inheritance | Yes | "Banned inheritance from 'NSObject'" | `@owned` freestanding classes | `test_banned_nsobject.m` ✓ |
| Untyped / dynamic receiver messaging | Yes | "Dynamic message dispatch requires an explicitly typed receiver" | Nominal devirtualization | `test_banned_untyped_msg.m` ✓ |
| Messaging nil on unproven pointers | Yes (returns nil) | "Banned nil messaging on unproven pointer" | `vvariant` for absence; proven-null via contracts | `test_banned_nil_msg.m` ⬜ |
| Runtime reflection (`NSClassFromString`, `objc_getClass`, `class_copyMethodList`) | Yes | "Banned Apple runtime reflection" | `@reflect()` at compile time | `test_banned_reflection.m` ✓ |
| Method swizzling (`method_exchangeImplementations`) | Yes | "Banned runtime method surgery" | None — method tables are immutable and link-time hashed | `test_banned_swizzling.m` ⬜ |
| Categories | Yes | "Banned categories" | Method overloads + sealed protocols | `test_banned_category.m` ⬜ |
| KVC / KVO (`setValue:forKey:`, `addObserver:`) | Yes | "Banned key-value coding/observation" | `@reflect` offsets; `vweak` observation | `test_banned_kvc.m` ⬜ |
| `@dynamic` properties / dynamic method resolution | Yes | "Banned dynamic property synthesis" | Checked, packed `@owned` ivars | `test_banned_dynamic_prop.m` ⬜ |
| `performSelector:` / `performSelector:afterDelay:` | Yes | "Banned deferred dynamic dispatch" | Direct calls; static task DAG | `test_banned_perform_selector.m` ⬜ |
| Lightweight generics (`NSArray<NSString *>`) | Yes | "Banned erased generic hints" | Real constrained `vgeneric` | `test_banned_nsarray_generics.m` ⬜ |
| Runtime thread spawn (`NSThread`, `dispatch_async`, GCD) | Yes | "Banned runtime scheduling on constrained paths" | Static topology; `vasync` fixed DAGs (`@cold`-only escape) | `test_banned_runtime_spawn.m` ⬜ |
| `@try` / `@catch` / `@finally` (unwinding) | Yes | "Banned exception unwinding" | `return err from f` (hidden flag, no unwinder) | `test_banned_try_catch.m` ⬜ |

### B. Memory & Ownership

| Banned construct | Valid ObjC/C? | Diagnostic (key phrase) | VeriObjC replacement | Test |
| :--- | :--- | :--- | :--- | :--- |
| Escaping data-stack reference | Yes (raw pointers) | "reference rooted at ... does not outlive the destination" | Pool handles; `vweak` | `test_banned_escaping_stack_ref.m` ✓ |
| Escaping function block | Yes | "Banned escaping function block" | Non-escaping blocks only | `test_banned_escaping_block.m` ✓ |
| Non-flat channel payload | Yes | "Banned non-flat channel payload" | Flat values; crossing = `memcpy` | `test_banned_nonflat_channel.m` ✓ |
| General heap on constrained paths | Yes (`malloc`) | "`@alloc` not permitted in `@nolatency` context" | Data stacks / `vpool` | `test_banned_heap_nolatency.m` ⬜ |
| Double free / use-after-move | — | Linear ownership violation | `move`/`consumed` tracking | `test_banned_double_free.m` ✓ |
| Unannotated `extern fn` called from `@nolatency` | Yes | "defaults to `@cold`" | Declared effects on `extern fn` | `test_cold_ffi_call.m` ✓ |
| Data-stack reference / pool slot across FFI | Yes | "Banned cross-boundary ownership" | Opaque `vpool` handles | `test_ffi_stack_ref_export.m` ✓ |
| Allocator mixing (foreign `free`, VeriObjC recycling of foreign memory) | Yes | "Banned allocator mixing" | Each allocator's products stay on its side | `test_banned_alloc_mixing.m` ⬜ |

### C. Metaprogramming (vmeta)

| Banned construct | Diagnostic (key phrase) | VeriObjC replacement | Test |
| :--- | :--- | :--- | :--- |
| Unconstrained type parameters | "Unconstrained type parameter" | `vgeneric` with declared constraints | `test_banned_unconstrained_generic.m` ⬜ |
| Specialization / SFINAE-style probing | "Banned specialization/probing" | Plain overloads; explicit `where` | `test_banned_specialization.m` ⬜ |
| Computation macros (preprocessor codegen) | "Banned computation macro" | `vconstexpr`; `@reflect`/`@objc_soa` codegen | `test_banned_computation_macro.m` ⬜ |
| Unbounded compile-time recursion | "Fuel exhausted (function, line, steps)" | Fueled interpreter — clean error, never a hang | `test_banned_fuel_exhaustion.m` ⬜ |
| `auto` in parameters / public signatures | "Banned inference in signature position" | Concrete types or constrained `vgeneric` | `test_banned_auto_param.m` / `test_banned_auto_export.m` ⬜ |
| `vregex` backreferences / lookaround | "No DFA can exist for this pattern" (with rewrite suggestion) | Regular-language dialect | `test_backref_regex.m` ✓ |
| Regex state explosion | "Byte budget exceeded (pattern named)" | Manifest table budget | `test_regex_state_explosion.m` ✓ |

### D. veriStream

| Banned construct | Diagnostic | Replacement | Test |
| :--- | :--- | :--- | :--- |
| Width-mismatched `zip` | "Stream width mismatch" | Compile-time equal-N check | `test_banned_stream_width_mismatch.m` ⬜ |
| Context exceeding `ContextCap` | "Context cap exceeded — compile error" | Pre-banked BRAM arenas | `test_banned_context_overflow.m` ⬜ |

---

## 📚 2. The Rejection Corpus (`examples/banned/`)

Files of **genuinely valid stock Objective-C** that VeriObjC must refuse. Each corpus file proves one thing: *this code is idiomatic ObjC, and the plugin still says no.*

**Corpus rules (the dual-direction gate):**

1. Every file must compile **cleanly under stock Clang 23** (`-ObjC`, `-Werror`, Foundation where used). If it doesn't compile as plain ObjC, the test proves nothing and is rejected from the corpus.
2. Every file must **fail under the plugin with the exact named diagnostic** — not a crash, not a generic error. The diagnostic string is asserted per file.
3. Every file maps to exactly one registry row above; a file covering two bans is split.
4. Naming: `examples/banned/valid_objc_<ban>.m` (e.g., `valid_objc_category_swizzle.m`, `valid_objc_nil_messaging.m`, `valid_objc_kvc_roundtrip.m`, `valid_objc_gcd_pipeline.m`, `valid_objc_lightweight_generics.m`).

**Representative corpus shape** (one file, one ban):

```objc
// valid_objc_category_swizzle.m
// Valid, idiomatic Objective-C. Banned in VeriObjC: runtime method surgery.
#import <Foundation/Foundation.h>
#import <objc/runtime.h>

@implementation NSString (VeriObjCBanCorpus)
+ (void)load {
    Method original = class_getInstanceMethod(self, @selector(length));
    Method swizzled = class_getInstanceMethod(self, @selector(veri_length));
    method_exchangeImplementations(original, swizzled);
}
- (NSUInteger)veri_length { return [self veri_length] + 1; }
@end
```

---

## 🧱 3. CI Gate: `ban_corpus_tests`

- **Passes iff:** 100% of the registry's corpus fails under the plugin **with the named diagnostic**, *and* 100% compiles clean under stock Clang 23. Both directions asserted on exit codes and diagnostic text.
- **Runbook:** `tests/integration/run_ban_corpus_test.sh` (CTest target `ban_corpus_tests`), alongside `examples_smoke_tests`.
- **Coverage discipline:** a registry row without a green test file is an open defect; a test file without a registry row is an undocumented ban — both fail the audit.
- **Survivor-report analogue:** `--effects-report`, `--branch-report`, `--mono-report`, and `--hw-report` tell you what survived *inside* valid code; the ban corpus proves what was refused *at the door*. Together they close the loop.

---

## 🗺️ Documentation Cross-Reference

- Registry rows marked ✓ are enforced by existing plugin tasks (TDD plan §2.2, §2.4, §2.5); rows marked ⬜ are scheduled tasks.
- The registry is the single source of truth: plugin diagnostics, the TDD plan's test files, and this corpus must all name the same bans. When a new ban is added to a spec canvas, a registry row and a corpus file are added in the same commit.