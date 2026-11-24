# ⚡ VeriObjC — Verifiable Objective-C

**A hardened, formally verifiable dialect of Objective-C for systems that cannot blink: bare metal, kernels, FPGAs, and ultra-low-latency engines.**

> **Status: Proof of Concept.** VeriObjC is an active research project, not a production language. Zero production hours.

---

## 📌 The Problem

Objective-C is a strict superset of C with an elegant object model — and it has been chained to a dynamic runtime (`libobjc`) for forty years. That runtime brings heap allocation, refcount churn, reflection, and non-deterministic dispatch: fine for desktops, disqualifying for bare metal, kernels, and latency-critical paths.

The usual escape is to drop to C and lose the object model, or to move to C++/Rust and lose the syntax and conventions you already have. VeriObjC takes a third path:

**Keep the Objective-C you know. Delete the runtime. Prove the rest.**

---

## 🎯 The Thesis

VeriObjC is a *closed-world* language: the compiler sees the entire program, so it can resolve at compile time what `libobjc` resolves at runtime — and prove properties that open-world languages cannot.

| Requirement | Standard Objective-C | VeriObjC | Runtime overhead |
| :--- | :--- | :--- | :--- |
| Method dispatch | Dynamic `objc_msgSend` search | Compile-time sealed jump matrix | **Zero** |
| Memory tracking | ARC refcount traffic | Compile-time linear ownership graph | **Zero** |
| Heap discipline | General `malloc`/`free` | Compiler-assigned data stacks + `vpool` | **Zero** (scope exit = one store) |
| Reference stability | Array realloc invalidates pointers | Grow-only stacks: references never move | **Zero** |
| Error handling | Exceptions / `NSError **` | `Result` + multi-frame `return err from f` | **Minimal** (no unwinder) |
| Closures | Heap-allocated `^` blocks | Non-escaping function blocks | **Zero** |
| Serialization | Frameworks + pointer fixups | Self-relative links; verified `from_bytes` | **Zero** (data layout *is* the format) |
| Correctness | Empirical testing | Z3 `QF_BV` SMT proofs on bounded `@pre`/`@post` paths | **Zero** (compile time) |

The catch: VeriObjC **cannot host runtime-loaded plugins, reflection, or an open class world** — the closed world *is* the product. If you need dynamism, VeriObjC is the wrong tool, by design.

---

## 🛠️ How It Works

```
[ VeriObjC source ] ──> [ veriobjc-cc (Clang 23 + Plugin) ] ──> [ Z3 QF_BV proof ] ──> [ Freestanding binary ]
                            (AST bans, CFG ownership,              (bounded-path           (zero libobjc, linked
                             IR static dispatch, reports)           @pre/@post contracts)    with libveriobjc_rt.a)
```

1. **Subtraction first.** A Clang AST plugin (`libVeriObjCPlugin.so`) rejects every runtime-dependent feature at compile time, with a named diagnostic for each ban.
2. **Then lowering.** An LLVM IR pass rewrites message sends on `@sealed`/`@final` hierarchies into direct calls and tag-switched dispatch tables — zero `objc_msgSend`, zero `-lobjc`.
3. **Then proof.** Bounded-path `@pre`/`@post` contract verification via Z3 (`--smt-verify` / `--smt-report`) over exact C bit-widths *before* the binary runs, emitting concrete counterexample witnesses on violation and failing closed on timeout.
4. **Then a micro-runtime.** A freestanding `libobjc` replacement (`libveriobjc_rt.a` + inline headers) in auditable C: no locks, no string tables, no `libobjc` dependency, no libc.

**Toolchain decision record** (why four languages): the Clang plugin API forces C++; the micro-runtime and `vstd` are C (freestanding, dogfooding the language's own memory rules); the package manager `vpm` is Go (bootstrap-independent); type-rule prototyping is OCaml/Haskell (throwaway).

---

## 🚀 Quickstart (Build, Install & Prove)

Bootstrap the reference Linux toolchain (Clang/LLVM 23, Z3 ≥ 4.8.12, CppUTest), run the 7 CTest suites, and install the relocatable `veriobjc-cc` SDK:

```bash
# 1. Bootstrap prerequisites (Ubuntu/Debian)
bash scripts/install.sh

# 2. Configure, build, and verify all 7 test suites
CC=clang-23 CXX=clang++-23 cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
ctest --test-dir build --output-on-failure

# 3. Install the self-contained SDK (veriobjc-cc, plugin, runtime archive, headers)
cmake --install build --prefix ~/.local
```

With `~/.local/bin` on `PATH`, `veriobjc-cc` compiles VeriObjC sources directly without manual `-Xclang` plugin flags or `-lobjc`:

```bash
# Compile & run a zero-heap, zero-objc_msgSend @sealed hierarchy
veriobjc-cc -O2 -Werror examples/sealed_mixed_scope.m -o /tmp/sealed_demo
/tmp/sealed_demo

# Discharge @pre/@post bit-vector contracts at compile time via Z3 QF_BV
veriobjc-cc --smt-verify --smt-report -fsyntax-only examples/vcontracts_bounds_proof.m

# Interoperate with freestanding C99 single-header libraries via -isystem + @cold staging
veriobjc-cc -O2 -Werror -isystem tests/plugin/isystem_ext \
  examples/isystem_c99_frame_stage.m -o /tmp/c99_interop_demo
```

---

## 💡 A Taste of the Language

A market-data parser (conceptual surface syntax; see [`examples/`](examples/) for current macro-annotated runnable sources). Scratch lives on a grow-only data stack — the reference captured by the first `push` stays valid a million pushes later, and the whole buffer is freed with **one store** at scope exit. Errors return across multiple frames with `return err from parse`, statically checked, no unwinder:

```objc
__attribute__((objc_sealed))
@interface MarketDataParser : VeriObject
- (uint32_t)parseInto:(vpool<InstrumentRef> *)out
                 from:(vstring_view)wire;   // zero-copy view of the DMA buffer
@end

@implementation MarketDataParser
- (uint32_t)parseInto:(vpool<InstrumentRef> *)out from:(vstring_view)wire {
    @scoped {                                        // scope exit = one store per stack
        auto fields = vstack_array<Field>::over(DMA_Hardware_Pool);
        for (auto &f : fields) {                     // lowered to a flat pointer loop
            if (!frame_ok(f)) {
                return err("bad frame") from parse;  // typed, multi-frame, no unwind
            }
            [out push:f];                           // built-in-place: no temporaries
        }
        return out->count;
    }
}
@end
```

Full runnable examples in [`examples/`](examples/) include `@sealed` mixed-size scope dispatch (`sealed_mixed_scope.m`), Z3 bounds proofs (`vcontracts_bounds_proof.m`), `@cold` static file staging (`host_staged_file_loader.m`), `-isystem` freestanding C99 interop (`isystem_c99_frame_stage.m`), fixed-point audio processing, and matmul-free ternary inference (`objc_hw_kernel` direct and transitive call-graph zero-multiplication check enforced).

---

## 🚫 What Gets Banned (and How You Know)

VeriObjC's design lives in its *impossible list*: 54 constructs rejected at compile time, each with a named diagnostic and a dedicated test — **a ban that isn't tested isn't a ban**.

| Category | Count | Highlights |
| :--- | :--- | :--- |
| Dynamic & nominal dispatch | 15 | `id`, `NSObject`, categories, KVC/KVO, swizzling, nil messaging, GCD spawns, `@try`/`@catch`, unsealed inheritance, raw casts to `@sealed`/`@final` |
| Memory & ownership | 23 | Escaping data-stack refs (incl. helper sinks), use-after-move (incl. struct/field aliases), FFI stack-ref export, non-flat channel payloads, allocator mixing, heap in `@nolatency` |
| Metaprogramming & attributes | 12 | Unconstrained generics, SFINAE-style probing, un-fueled `vconstexpr`, non-exhaustive `vmatch`, Z3 `QF_BV` contract counterexamples, unrecognized `annotate("veriobjc_*")` spellings |
| Hardware streams & 0-DSP | 4 | Width-mismatched `zip`, `ContextCap` overflow, direct/transitive runtime multiply/divide or opaque indirect/`extern` calls in `objc_hw_kernel` |

The **Valid-ObjC Rejection Corpus** (`examples/banned/`) is dual-direction gated in CI: every file must compile `-Werror` clean under stock Clang *and* be rejected by the plugin with the exact named diagnostic. A file that isn't valid Objective-C proves nothing.

---

## 🧪 Verification & Measurement Discipline

- **Bounded-path proofs, honestly stated.** `@pre`/`@post` contracts on bounded pure/FFI functions are discharged at compile time by Z3 (`>= 4.8.12`) over exact C two's-complement bit-vectors (`QF_BV`) via `--smt-verify` and `--smt-report` (`Z3_UNSAT_PROVEN`, `Z3_SAT_COUNTEREXAMPLE` with concrete witness values, or fail-closed `Z3_UNKNOWN_TIMEOUT`). We target bounded-path verification — *not* mathematical totality (undecidable) and *not* Rust's whole-language soundness (a different, per-function, open-world guarantee).
- **Effects are typed.** `@alloc`/`@io`/`@blocking`/`@fence`/`@shared-retain` and `@cold` init quarantines are part of every signature; an effectful or `@cold` call inside a `@nolatency` path is a compile error.
- **Reports are CI artifacts.** `--effects-report`, `--branch-report`, and `--mono-report` are diffed per build: a hot loop that *gains* an effect or an unproven branch fails review even when all tests pass.
- **No number, no claim.** Latency budgets are versioned artifacts; benchmarks publish losses alongside wins, matching the measurement discipline common among HFT firms.

---

## 📊 Project Status

| Area | State |
| :--- | :--- |
| **Phases 0–2** (toolchain, micro-runtime, Clang plugin + LLVM IR pass: bans, `clang::CFG` move/escape dataflow, transitive zero-DSP call-graph check, standalone `@sealed` tag-switched polymorphism without `-lobjc`, `vgeneric`, `vconstexpr`, `vregex`, `veriStream` AST elaboration, ban corpus) | ✅ Implemented — 100% region/function/branch coverage on `rt/`, audited via `llvm-cov`, `nm`, and `llvm-objdump` |
| **Phase 3** (`vstd/`, `sys0/`, `hw/`, `<vstd/contracts.h>`, `<vstd/simd.h>`, `<vstd/io.h>`, header-closure & minimal-binary cohesion gate) | ✅ Implemented — verified by `unit_rt_tests`, `plugin_tests`, `ban_corpus_tests`, and `vstd_cohesion_tests` |
| **Phase 4** (cycle benchmark harness, survivor report diff engine, latency budget gate, Z3 `QF_BV` SMT contract verification) | ✅ Implemented — bench harness, survivor diff, latency gate, and real `z3 -smt2 -in` `QF_BV` contract discharge (`--smt-verify`, `--smt-report`, `--smt-timeout-ms`, `--z3-path`) pass unit and plugin tests (`TestSmtContractBridge.cpp`, `test_valid_smt_z3_proof.m`, `test_banned_smt_z3_counterexample.m`) |
| **Phase 5** (examples smoke suite, end-to-end freestanding `@sealed` polymorphism & mixed-size `vspan` scope composition without `-lobjc`, `VOBJ` snapshot persistence, pure-CPU ternary fallback, transitive ternary zero-multiplication check) | ✅ Implemented — Tasks 5.1–5.3, Task 5.5 mixed-size `@sealed` upcast dispatch + single-store scope reset + escape policing (`e2e_sealed_polymorphism_tests`, `examples/sealed_mixed_scope.m`), `VOBJ` v1 golden corpus (`fixtures/vobj_v1/`), and pure-CPU round-half-up ternary fallback (`<vstd/ternary.h>`) pass *(Task 5.4 RTL synthesis/co-sim is out of scope for standalone `veriObjC`)* |
| **Phase 6** (`veriobjc-cc` SDK driver, `vosh` scripting runner & `vpm` package manager) | 🟡 In Progress — **Block 6.0 complete (`6.0.1`–`6.0.10`)**: `cmake --install` relocatable SDK & `veriobjc-cc` driver (`veriobjc_cc_sdk_tests`), `.objc_*` COMDAT guard, `libveriobjc_rt.a` archive hygiene, unknown `annotate(...)` rejection, `@cold` static staging file loader (`<veriobjc_host_io.h>`), `-isystem` C99 interop, deterministic `check_vstd_cohesion.sh` gate, transitive helper-sink & field-alias CFG checks, fail-closed `objc_hw_kernel` 0-DSP call graph, and `@sealed`/`@final` raw-cast ban (`7/7` CTest suites passing); `vosh` and `vpm` next |
| **Toolchain** | Clang / LLVM 23.1.5, Z3 ≥ 4.8.12 (compile-time host verifier), CppUTest, CMake |
| **Production usage** | None. This is a research bet that the *impossible list* is worth the closed world. |

---

## 📚 Documentation

| Document | Contents |
| :--- | :--- |
| [`docs/veriobjc-specification.md`](docs/veriobjc-specification.md) | Architectural specification: pipeline, memory model, syntax inventions |
| [`docs/veriobjc-poc-execution-plan.md`](docs/veriobjc-poc-execution-plan.md) | PoC execution plan, language decision record, acceptance criteria |
| [`docs/veriobjc-ban-list.md`](docs/veriobjc-ban-list.md) | Ban Registry (54 bans, diagnostics, tests) + rejection corpus rules |
| [`docs/veriobjc-foundational-library.md`](docs/veriobjc-foundational-library.md) | `vstd` design: strings, stacks, variants, channels, atomics, FFI matrix |
| [`docs/veriobjc-advanced-substrate.md`](docs/veriobjc-advanced-substrate.md) | vmeta metaprogramming, vregex, `veriStream` fixed-point streaming substrate |
| [`docs/veriobjc-type-system-and-metaprogramming-vmeta.md`](docs/veriobjc-type-system-and-metaprogramming-vmeta.md) | Nominal local type inference (`auto`), constrained `vgeneric`, `vconstexpr`, `vregex` |
| [`docs/veriobjc-master-library-mapping-matrix.md`](docs/veriobjc-master-library-mapping-matrix.md) | Master library mapping matrix across standard, low-level, and hardware tiers |
| [`docs/veriobjc-examples.md`](docs/veriobjc-examples.md) | Architectural design walkthroughs and domain examples |
| [`docs/veriobjc-proof-of-concept.md`](docs/veriobjc-proof-of-concept.md) | Proof-of-concept design and baseline verification targets |
| [`docs/veriobjc-language-comparison.md`](docs/veriobjc-language-comparison.md) | Honest dimension-by-dimension comparison vs ObjC/C++/Rust/Zig/Go |
| [`plan.md`](plan.md) | Master TDD execution checklist (this repo's living build log) |

---

## 🙏 Influences

- **[Goose](https://aardappel.github.io/goose/)** — the reference language for the stack-first memory model, built-in-place construction, flat-value channels, `return err from`, self-relative links, and function blocks. VeriObjC adopts these mechanisms and adapts them to the Objective-C object model.
- **Rust** — ownership discipline and `Result`-style errors (VeriObjC demands a closed world in exchange for stronger bounded-path claims).
- **Zig** — explicit allocators, "no hidden control flow", fueled compile-time evaluation.
- **C++** — layout control, contracts (P2900), `constexpr` instinct (without templates-as-TMP).

## 🔗 See Also

- [`INSTALL.md`](INSTALL.md) — toolchain bootstrap on a fresh machine
- [`BUILDING.md`](BUILDING.md) — toolchain setup (Clang 23, CppUTest, CMake) and `veriobjc-cc` CLI guide
- [`CONTRIBUTING.md`](CONTRIBUTING.md) — TDD workflow: no task merges without its red test first
- [`examples/README.md`](examples/README.md) — guide to positive examples and the Valid-ObjC Rejection Corpus
- [`docs/coming-to-veriobjc-a-guide-by-programming-background.md`](docs/coming-to-veriobjc-a-guide-by-programming-background.md) — "Coming to VeriObjC" from ObjC, C++, Rust, or Go
