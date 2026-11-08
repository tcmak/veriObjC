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
| Correctness | Empirical testing | SMT proofs (SMACK/Z3) on bounded paths | **Zero** (compile time) |

The catch: VeriObjC **cannot host runtime-loaded plugins, reflection, or an open class world** — the closed world *is* the product. If you need dynamism, VeriObjC is the wrong tool, by design.

---

## 🛠️ How It Works

```
[ VeriObjC source ] ──> [ Clang AST plugin ] ──> [ SMACK/Z3 proof ] ──> [ Bare-metal binary ]
                            (bans, ownership,              (bounded-path            (linked with the
                             lowering, reports)             memory & contracts)       freestanding micro-runtime)
```

1. **Subtraction first.** A Clang plugin rejects every runtime-dependent feature at the AST level, with a named diagnostic for each ban.
2. **Then lowering.** Message sends become direct calls; sealed hierarchies become jump tables in `.rodata`; `@pre`/`@post` contracts become proof obligations.
3. **Then proof.** Bounded-path memory safety and contract verification via SMACK/Z3, *before* the binary exists.
4. **Then a micro-runtime.** A freestanding `libobjc` replacement in auditable C: no locks, no string tables, no `isa` tax, no libc.

**Toolchain decision record** (why four languages): the Clang plugin API forces C++; the micro-runtime and `vstd` are C (freestanding, dogfooding the language's own memory rules); the package manager `vpm` is Go (bootstrap-independent); type-rule prototyping is OCaml/Haskell (throwaway).

---

## 💡 A Taste of the Language

A market-data parser. Scratch lives on a grow-only data stack — the reference captured by the first `push` stays valid a million pushes later, and the whole buffer is freed with **one store** at scope exit. Errors return across multiple frames with `return err from parse`, statically checked, no unwinder:

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

Full examples: [`examples/`](examples/) — fixed-point audio processing, the market-data parser above, and a matmul-free ternary-inference block for FPGA synthesis (DSP blocks = 0, enforced by CI).

---

## 🚫 What Gets Banned (and How You Know)

VeriObjC's design lives in its *impossible list*: 30 constructs rejected at compile time, each with a named diagnostic and a dedicated test — **a ban that isn't tested isn't a ban**.

| Category | Count | Highlights |
| :--- | :--- | :--- |
| Dynamic runtime | 13 | `id`, `NSObject`, categories, KVC/KVO, swizzling, nil messaging, GCD spawns, `@try`/`@catch` |
| Memory & ownership | 8 | Escaping data-stack refs, non-flat channel payloads, allocator mixing, heap in `@nolatency` |
| Metaprogramming | 7 | Unconstrained generics, SFINAE-style probing, un-fueled compile-time evaluation |
| Hardware streams | 2 | Width-mismatched `zip`, context overflow beyond declared `ContextCap` |

The **Valid-ObjC Rejection Corpus** (`examples/banned/`) is dual-direction gated in CI: every file must compile `-Werror` clean under stock Clang *and* be rejected by the plugin with the exact named diagnostic. A file that isn't valid Objective-C proves nothing.

---

## 🧪 Verification & Measurement Discipline

- **Bounded-path proofs, honestly stated.** `@pre`/`@post` contracts and memory-safety obligations are discharged by SMACK/Z3 across bounded paths before codegen. We claim bounded-path verification — *not* mathematical totality (undecidable) and *not* Rust's whole-language soundness (a different, per-function, open-world guarantee).
- **Effects are typed.** `@alloc`/`@io`/`@blocking`/`@fence`/`@shared-retain` are part of every signature; an effectful call inside a `@nolatency` loop is a compile error.
- **Reports are CI artifacts.** `--effects-report`, `--branch-report`, `--mono-report`, and `--hw-report` are diffed per build: a hot loop that *gains* an effect or an unproven branch fails review even when all tests pass.
- **No number, no claim.** Latency budgets are versioned artifacts; benchmarks publish losses alongside wins, matching the measurement discipline common among HFT firms.

---

## 📊 Project Status

| Area | State |
| :--- | :--- |
| **Phases 0–2** (toolchain, micro-runtime, Clang plugin: bans, devirtualization, lowering, `vgeneric`, `vconstexpr`, `vregex`, `veriStream` elaboration, ban corpus ) | ✅ Complete — 100% region/function/branch coverage on `rt/`, audited via `llvm-cov` |
| **Phases 3–6** (vstd incl. device I/O, SIMD, contracts; latency CI, verification harness (SMACK/Z3), `vpm` & manifest, FPGA zero-DSP gate) | ⬜ Planned |
| **Toolchain** | Clang / LLVM 23.1.5, CppUTest, CMake |
| **Production usage** | None. This is a research bet that the *impossible list* is worth the closed world. |

---

## 📚 Documentation

| Document | Contents |
| :--- | :--- |
| [`docs/veriobjc-specification.md`](docs/veriobjc-specification.md) | Architectural specification: pipeline, memory model, syntax inventions |
| [`docs/veriobjc-poc-execution-plan.md`](docs/veriobjc-poc-execution-plan.md) | PoC execution plan, language decision record, acceptance criteria |
| [`docs/veriobjc-ban-list.md`](docs/veriobjc-ban-list.md) | Ban Registry (30 bans, diagnostics, tests) + rejection corpus rules |
| [`docs/veriobjc-foundational-library.md`](docs/veriobjc-foundational-library.md) | `vstd` design: strings, stacks, variants, channels, atomics, FFI matrix |
| [`docs/veriobjc-advanced-substrate.md`](docs/veriobjc-advanced-substrate.md) | vmeta metaprogramming, vregex, `veriStream` fixed-point streaming substrate |
| [`docs/veriobjc-type-system-and-metaprogramming-vmeta.md`](docs/veriobjc-type-system-and-metaprogramming-vmeta.md) | Nominal local type inference (`auto`), constrained `vgeneric`, `vconstexpr`, `vregex` |
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
- [`BUILDING.md`](BUILDING.md) — toolchain setup (Clang 23, CppUTest, CMake) and CLI guide
- [`CONTRIBUTING.md`](CONTRIBUTING.md) — TDD workflow: no task merges without its red test first
- [`examples/EXAMPLES.md`](examples/EXAMPLES.md) — guide to positive examples and the Valid-ObjC Rejection Corpus
- [`docs/coming-to-veriobjc-a-guide-by-programming-background.md`](docs/coming-to-veriobjc-a-guide-by-programming-background.md) — "Coming to VeriObjC" from ObjC, C++, Rust, or Go
