# ⚖️ VeriObjC: Comparison with Traditional ObjC, C++, Rust, Zig, and Go

**The honest framing first:** VeriObjC is a design and a PoC; the other five are decades-mature languages with millions of production hours. This comparison is not "which language is better" — it is **what each language chooses to make impossible**, and what that choice costs. VeriObjC's thesis is that for constrained low-latency systems (HFT, bare metal, massively parallel compute), the *impossible* list is the product.

---

## 🧠 Memory Model

| Language | Model | What bites in practice |
| :--- | :--- | :--- |
| **Traditional ObjC** | ARC heap refcounting; everything is a heap object; autorelease pools | Dealloc waves cause latency spikes; retain cycles leak; refcount traffic on hot paths |
| **C++** | Manual heap + stack; RAII; smart pointers as conventions | Heap fragmentation; invisible copies; smart-pointer aliasing; leaks under early returns |
| **Rust** | Ownership + borrow checking; heap via `Box`/`Rc`/`Arc` as needed | Borrow-checker fights are the learning curve; `Arc<Mutex<T>>` trees on shared state |
| **Zig** | Manual; allocators passed as explicit parameters | Correct but unverified — use-after-free still possible |
| **Go** | Garbage collected | Sub-millisecond but nonzero pauses; GC tuning as a profession |
| **VeriObjC** | **Data stacks** (grow-only bump, stable references, scope exit = one store) + **`vpool`** (freelist reuse, generation-counted `vweak` detects staleness); `@shared` refcounting quarantined to `@cold` | You must think in regions and flat values; sharing across cores is structurally absent |

## 🏃 Runtime & Dispatch

| Language | Dispatch | Runtime dependency |
| :--- | :--- | :--- |
| **ObjC** | Dynamic `objc_msgSend`; open world; runtime method tables | libobjc.dylib is mandatory — the exact dependency VeriObjC deletes |
| **C++** | Virtual vtables; RTTI | Minimal runtime, but ABI fragmentation across compilers |
| **Rust** | Static monomorphization by default; `dyn Trait` when needed | No runtime; panics unwind unless `-C panic=abort` |
| **Zig** | Compile-time (`comptime`); explicit vtables | None |
| **Go** | Interface `itab` dynamic dispatch | Full runtime: GC, scheduler, goroutines |
| **VeriObjC** | **Closed world**: nominal devirtualization; sealed protocols; link-time perfect-hashed witness tables in `.rodata` | Micro-runtime, freestanding-capable; zero `objc_msgSend`, zero string lookup tables |

## 🧬 Generics & Compile-Time Computation

| Language | Generics | Compile-time compute | Failure mode |
| :--- | :--- | :--- | :--- |
| **ObjC** | Erased hints (`NSArray<NSString *>`) — no generic functions at all | None (runtime reflection instead) | Dynamism instead of types |
| **C++** | Templates — Turing-complete by accident | Template metaprogramming, `constexpr` | **The OOM story**: errors at the 47th instantiation; unbounded instantiation |
| **Rust** | Constrained traits, monomorphized | `const fn`, macros | Long compile times; opaque bound errors |
| **Zig** | `comptime` duck typing | `comptime` evaluation | Type errors surface late |
| **Go** | Type parameters (1.18+), constrained | Limited (`go:generate` external tools) | Clunky constraint syntax |
| **VeriObjC** | **`vgeneric`**: checked at the definition, once; `@mono`/`@erased` strategies chosen explicitly | **`vconstexpr` fueled interpreter** — step/depth/allocation budgets, memoized; `vtrait` closed tables; `vregex` compiles to DFA tables | Budget- or fuel-exceeded is a **clean error naming the function** — the compiler cannot OOM; `--mono-report` makes instantiation cost reviewable |

## 🚨 Error Handling

| Language | Mechanism | Cost |
| :--- | :--- | :--- |
| **ObjC** | `NSError **` + nil-messaging tolerance | Errors silently swallowed; messaging nil returns zero |
| **C++** | Exceptions | Unbounded unwinding — banned in most HFT shops anyway |
| **Rust** | `Result` + `?` | Explicit, allocation-free — the model VeriObjC builds on |
| **Zig** | Error unions `!T`, `errdefer` | Explicit and good |
| **Go** | Error values, `if err != nil` | Explicit but verbose; wrapping culture grew frameworks |
| **VeriObjC** | Typed error discriminants + **`return err from f`** (multi-frame, no unwinder — a hidden flag per frame) | Uncaught `from` at top level is a compile error; zero unwinding cost |

## 🔀 Concurrency

| Language | Model | Determinism |
| :--- | :--- | :--- |
| **ObjC** | NSThread / GCD dynamic work-stealing | Scheduler jitter |
| **C++** | std::thread; shared memory; locks | Data races are UB, not errors |
| **Rust** | Fearless concurrency — *data-race freedom*, not determinism; async/await state machines | Send/Sync checked; timing still unbounded |
| **Zig** | Manual threads; async colored functions | Manual discipline |
| **Go** | Goroutines + scheduler; channels can share pointers | GC pauses + scheduler jitter; races possible |
| **VeriObjC** | **Static topology**: cores declared at build; **flat-value-only `vatomic_channel`s** (crossing = memcpy); fixed task DAGs with computed WCET and proven deadlock-freedom | Data races are structurally absent — a data-stack reference or `@shared` object crossing a channel is a **compile error** |

## 🛡️ Safety & Verification

| Language | Safety story | Proof |
| :--- | :--- | :--- |
| **ObjC** | Dynamic messaging, nil-tolerance | None |
| **C++** | Conventions; UB everywhere | None built in |
| **Rust** | Memory safety via borrow checking — **per function, works on open worlds** | Soundness proofs for the language, not your program |
| **Zig** | Explicit but unchecked | None |
| **Go** | Memory safety via GC (data races still possible) | None |
| **VeriObjC** | Linear Ownership Graph (root checks, move/borrow/`vweak` generations) + design-by-contract to SMACK/Z3 + effect quarantine | **Bounded-path verification**: `@pre`/`@post` proven across bounded paths before the binary exists; `--effects-report` proves a certified loop effect-free |

**The honest trade:** Rust proves memory safety *per-function without seeing the whole program*. VeriObjC demands a **closed world** and gives bounded-path proofs plus WCET — stronger claims, harder admission. VeriObjC cannot host runtime-loaded plugins by design; Rust can. Whole-program unbounded proof remains undecidable — the claim is bounded-path, not mathematical totality.

## ⏱️ Determinism & Latency

| Language | Story |
| :--- | :--- |
| **ObjC** | Refcount waves, autorelease pools, scheduler |
| **C++** | Fast *on average*; zero guarantees — UB and allocator jitter live here |
| **Rust** | Deterministic-ish: no GC, but unwinding, allocator, and unbounded loops remain |
| **Zig** | No hidden control flow — honest, but nothing is *proven* |
| **Go** | Explicitly not a latency language |
| **VeriObjC** | **Effects are typed** (`@alloc`/`@io`/`@blocking`/`@fence`/`@shared-retain`); effectful calls in `@nolatency` loops are compile errors; `--effects-report`, `--branch-report`, `--mono-report` are CI-diffed survivor reports; one-store scope frees; WCET computable (provable on bare metal, measurable on Linux) |

## 🌉 Interop

| Language | Reaching out |
| :--- | :--- |
| **ObjC/C++/Zig** | C ABI natively |
| **Rust** | C ABI; ecosystem-rich but toolchain-pinned |
| **Go** | cgo — costly enough to ban from hot paths anywhere |
| **VeriObjC** | **C ABI both directions**: C imports first-class (ObjC is a C superset), Zig near-first-class, Rust via C ABI, C++ wrapper-only, Go `@cold`-only. `extern fn` must declare effects (unannotated = `@cold`); flat values or opaque `vpool` handles cross; no allocator mixing |

## 📦 Maturity — No Number, No Claim

| | VeriObjC | Others |
| :--- | :--- | :--- |
| Production hours | **Zero (PoC)** | Decades each, millions of systems |
| Ecosystem | Design-stage `vstd` specification | Crates.io, PyPI-scale ecosystems, 40 years of C++ libraries |
| Compilers | One Clang plugin (planned) | Battle-tested, multiple independent implementations |
| Verdict | A research-backed bet that the *impossible list* is worth the closed world | The boring, defensible default for everything except constrained paths |

---

## 🎯 What VeriObjC Takes, Rejects, and Owes Each Language

| Language | VeriObjC borrows | VeriObjC rejects | That language still does better |
| :--- | :--- | :--- | :--- |
| **ObjC** | The syntax, bracket messaging, protocols, C superset | libobjc, dynamism, categories, KVC/KVO, nil-messaging tolerance | Rapid application development — dynamism is a *feature* for GUI/plugin ecosystems, not a bug |
| **C++** | C superset, layout control, RAII instinct, contracts (P2900), `variant`/`visit`, `constexpr`, execution (P2300) | Templates-as-TMP, exceptions, SFINAE, CRTP, heap-first defaults, UB tolerance | Ubiquity, toolchain maturity, library depth; VeriObjC covers a fraction of this surface by design |
| **Rust** | Ownership discipline (`move`/`borrow`/`vweak`), `Result`-style errors, sealed traits, zero runtime | Heap-centric ownership, `Rc`/`Arc`/`RefCell`, macro systems, open-world crates | General-purpose safety without a closed world; ecosystem; hosting dynamically loaded code |
| **Zig** | `comptime`-style compile-time evaluation (with fuel), explicit allocators, "no hidden control flow", C-first interop, bundled toolchain bootstrap | Pre-1.0 instability (nothing — VeriObjC borrows the philosophy wholesale) | Simplicity and bootstrapping; cross-compilation as a product feature |
| **Go** | Errors-as-values philosophy, single-binary deployment | GC, goroutines, dynamic interfaces, `reflect`-driven serialization (encoding/json culture) | Team velocity, network services, readability at scale — VeriObjC is deliberately hostile to that flexibility |

---

## 🧭 Which Language for Which Job

| Job | Reach for |
| :--- | :--- |
| HFT hot path, constrained loop with proof obligations | **VeriObjC** (or disciplined C++ today) |
| General systems programming, open ecosystem, safety without a closed world | **Rust** |
| Ubiquity, game engines, existing codebases | **C++** |
| Network services, team velocity | **Go** |
| Bootstrapping, build tooling, small auditable systems | **Zig** |
| Apple GUI applications | **ObjC/Swift** — VeriObjC explicitly declines this job |