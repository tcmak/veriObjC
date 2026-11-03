# 🧭 Coming to VeriObjC: A Guide by Programming Background

What you need to unlearn depends on where you arrive from. This guide covers four backgrounds — **traditional Objective-C, C++, Rust, and Go** — with the mindset shift, a translation table, the habits to unlearn, and the compile errors you'll meet in your first week.

## ⚖️ The Five Rules That Apply to Everyone

Before the background-specific guidance, these are the rules every VeriObjC author lives under:

1. **The world is closed.** Every type, method, and call site is known at link time. There is no runtime-loaded code, no plugins, no `NSClassFromString`. The compiler sees everything, so it can prove things.
2. **Effects are typed.** `@alloc`, `@io`, `@blocking`, `@fence`, `@shared-retain` are part of signatures. An effectful call inside a `@nolatency` context does not compile. Unannotated FFI defaults to `@cold`.
3. **`@cold` is the quarantine, not the toolbox.** Diagnostics, string formatting, refcounted `@shared` objects live there. It is a hard type barrier: `@cold` cannot be called from constrained contexts, period.
4. **Flat values cross boundaries.** Channels, FFI, and core boundaries accept contiguous, reference-free values only. Crossing is a `memcpy`.
5. **Every cost is budgeted and reported.** Instantiations, compile-time fuel, effects, unproven branches — all finite, all in CI-diffed reports (`--mono-report`, `--effects-report`, `--branch-report`). The compiler cannot OOM, and neither can your binary.

---

## 📱 From Traditional Objective-C

**You'll feel at home because:** the syntax is yours — bracket messaging, protocols, `@interface`, `#import`. VeriObjC is ObjC syntax compiled as a C superset with the dynamism surgically removed.

### Translation Table

| Traditional ObjC habit | VeriObjC reality |
| :--- | :--- |
| `[nil doSomething]` returns zero, life goes on | **Compile error** on unproven pointers. Nil-tolerance is gone; absence is modeled by `vvariant`, not by messaging nothing |
| `id` as a generic "some object" | **Banned keyword.** Use sealed protocols, concrete types, or `vvariant` for closed sets |
| `NSObject` inheritance chain | `@owned` headerless classes: no `isa`, no prefix bytes, `sizeof` = sum of fields |
| ARC retain/release | Data stacks + `vpool`. Scope exit is one store; no refcount traffic |
| Categories to patch someone's class | **Banned.** Method overloads and sealed protocols only |
| KVC / KVO / runtime reflection | **Banned.** `@reflect()` at compile time; offsets are constants in the binary |
| Method swizzling | **Banned.** The method tables are immutable and link-time hashed |
| `^` blocks | Function blocks: monomorphized, non-escaping, zero allocation. A block stored into an escaping location is a compile error |
| `NSError **` out-params | Typed error discriminants + `return err from f` |
| `NSArray` / `NSDictionary` | `vvector<N>` (fixed-capacity, contiguous) / `vhash_flat` (open-addressed, probe-bounded) |
| Message dispatch (cache lookup, dynamic) | Nominal devirtualization: known receiver = direct C call; sealed protocol = link-time witness table |

### Unlearn These Habits

- **"The heap is where objects live."** Objects live in data-stack frames and pools. The heap is a `@cold` luxury.
- **"I'll check the type at runtime."** There is no runtime type system to check with. Everything is decided at compile time or designed out.
- **"A category can fix that later."** The closed world means *later* is a recompile, and there is no other kind of fix.

### First-Week Compile Errors

Using `id`; declaring a category; a KVC string; messaging an unproven nullable pointer; trying to store a block somewhere it could escape.

---

## 🔧 From C++

**You'll feel at home because:** it's a C superset, you already care about layouts and cache lines, your RAII instinct maps to scope-exit discipline, and `vconstexpr` is `constexpr` done honestly. The language is shaped by the hard-won practices common on HFT C++ desks.

### Translation Table

| C++ habit | VeriObjC reality |
| :--- | :--- |
| RAII destructor runs arbitrary code at scope exit | Scope exit is **one store per data stack**. Cleanup has no code; you model resource release with `@scoped`, pools, and contracts — never "the destructor will handle it" |
| `new` / `delete`, `std::make_unique` | Data stacks (compiler-assigned) and `vpool` (recycled). The heap is not the default anything |
| `std::unique_ptr` | `move` parameters — caller provably loses ownership |
| `std::shared_ptr` | **Quarantined.** `@shared` refcounting lives in `@cold`; on constrained paths, `vweak` generation-counted handles observe pool objects |
| Move semantics / `std::move` dance | **Built-in-place construction**: `items.push(parse(line))` writes at the destination through any call depth — temporaries and defensive copies are checked away by the plugin, not hoped away by the optimizer |
| Templates with `typename T` | `vgeneric` — every parameter constrained, body checked **once at the definition** |
| Specialization / SFINAE / `enable_if` | **Banned.** Plain overloads and explicit `where` clauses at the definition |
| Template metaprogramming (fold, recursion) | `vconstexpr` interpreter with **fuel** — a step/depth/allocation budget. Exhaustion is a clean error naming the function |
| `constexpr` tables | `consteval` / `vconstexpr` arrays, memoized program-wide; budgets in the manifest |
| Exceptions | Typed errors + `return err from f`. No unwinder exists |
| `virtual` / vtables | Sealed protocols with link-time perfect-hashed witness tables; `@final` direct calls |
| `std::function` / heap lambdas | Function blocks — inlinable, non-escaping, zero allocation |
| `std::vector` | `vvector<N>` — fixed capacity, contiguous, no growth reallocation (stable references by construction) |
| `std::variant` + `std::visit` | `vvariant` + exhaustive `match` — jump tables, two storage modes |
| Signed overflow, uninitialized reads (UB) | Compile errors under the closed world — UB is the runtime behavior VeriObjC exists to delete |

### Unlearn These Habits

- **"The allocator is invisible infrastructure."** Every allocation is visible, typed, and counted against a context that may not allow it.
- **"Template metaprogramming is a toolbox."** It is the hazard the fuel budget exists to contain. Your cleverest TMP trick is a compile error here — and the honest version is a readable `vconstexpr` loop.
- **"Exceptions for exceptional paths."** There is no unwinding machinery in the binary at all.
- **"The optimizer will elide the copy."** The plugin proves elision structurally (built-in-place) instead.

### First-Week Compile Errors

Unconstrained `typename`-style generic; a specialization; recursive instantiation blowing fuel; an `@alloc` inside a `@nolatency` loop; a block stored into a `vstrong` field that outlives its root.

---

## 🦀 From Rust

**You'll feel at home because:** the ownership taxonomy is a sibling — `move`/`borrow`/`vweak` ≈ move/references/`Weak`, errors are typed values with propagation sugar (`return err from f` ≈ `?`), traits are sealed and dispatch is explicit, and there is no runtime. You already think in ownership; VeriObjC asks you to think in *memory regions*.

### Translation Table

| Rust habit | VeriObjC reality |
| :--- | :--- |
| `&T` / `&mut T` | `borrow T` — rooted at the caller's scope; storing it anywhere longer-lived is a compile error |
| `Box<T>` as default ownership | **No Box.** Pools and data stacks; the heap is a `@cold` affordance |
| `Rc` / `Arc` / `clone()` | `@shared` refcounting quarantined to `@cold`. On constrained paths: restructure to flat values, or observe pools via `vweak` (generation-counted — a stale handle *reports* stale instead of reading garbage) |
| `RefCell` / interior mutability | Doesn't exist. Cross-core state goes through flat-value channels; within a core, ownership is linear and explicit |
| `mpsc` channels | `vatomic_channel` — payloads must be **flat**; crossing is a `memcpy`; sending a non-flat value or a data-stack reference is a compile error |
| `dyn Trait` trait objects | `@erased` generics: one shared instantiation + link-time witness tables (possible because the world is closed) |
| `Result<T, E>` + `?` | Typed error discriminants + `return err from f` — propagates through intervening frames with a hidden flag, no unwinding |
| `enum` + exhaustive `match` | `vvariant` + `match` — same discipline, plus packed per-variant storage and jump tables |
| `const fn` / const generics | `vconstexpr`/`consteval` with fuel; `vgeneric <N : vconstexpr uint32_t>` |
| `macro_rules!` / `derive` | `@reflect()` and `@objc_soa` codegen — in-toolchain, output visible in the AST dump |
| `async`/`await`, executors | `vasync` stack-allocated state machines with explicit hardware wake sources and **static task DAGs** — no runtime scheduler, no executor crate |
| `unsafe` escape hatch | **Does not exist.** The escape hatch is `@cold` — a typed, quarantined zone, not a locally-unsound block |
| Send/Sync bounds | Not needed: cores are share-nothing by construction; there is no shared memory to mark |
| cargo workspaces | Content-addressed, effect-aware packages with manifest-declared instantiation budgets |

### Unlearn These Habits

- **"Box it and move on."** Heap-as-default is the Rust design VeriObjC inverts. Ask *which region* a value lives in before asking what owns it.
- **"Clone to appease the checker."** Copies are structural (built-in-place) and budgeted; reaching for a copy is a design smell.
- **"An `Arc<Mutex<T>>` tree is fine if the borrow checker passes."** It passes nothing here — the concurrency model is flat channels between static cores, not shared protected state.
- **"Crates compose on an open world."** The closed world is the price of the proofs. There is no dynamic loading, ever.

### First-Week Compile Errors

Sending a `vpool` reference through a channel; an `@shared` retain on a constrained path; a borrow escaping its root; forgetting that a `vweak` must `guard` (generation check + re-root) before use on a constrained path.

---

## 🐹 From Go

**You'll feel at home because:** errors are values, binaries are single and static, and the philosophy of explicit small mechanisms is shared. The other 90% is different: the GC, the scheduler, and dynamic interfaces — Go's three pillars — are precisely what VeriObjC deletes.

### Translation Table

| Go habit | VeriObjC reality |
| :--- | :--- |
| `go func()` anywhere | **Static topology.** Cores, affinities, and thread assignments are declared at build time; there is no spawn at run time |
| Goroutine scheduler | Nothing schedules. Fixed task DAGs with computed WCET and proven deadlock-freedom |
| `chan T` (sharing pointers through it) | `vatomic_channel` — **flat values only**; crossing is a `memcpy`. Sharing by communicating becomes *copying, provably* |
| `interface{}` / `any` | **Banned.** Closed sets are `vvariant` + `match`; open polymorphism is sealed protocols |
| `defer` chains | `@scoped` — scope exit is one store per data stack; no arbitrary deferred code |
| `if err != nil` ladders | Typed error discriminants + `return err from f`; failures compose through frames without the ladder |
| `errors.Is/As`, wrapping culture | Errors are typed values with a discriminant per frame — structural, not stringly |
| Generics (1.18 type parameters) | `vgeneric` — mandatory constraints, definition-site checking, explicit `@mono`/`@erased` strategy |
| Closures capturing variables | Function blocks — compile-time, non-escaping, zero allocation |
| `reflect` / `encoding/json` | **Banned.** Wire formats are `__packed` structs + layout asserts; variable-shaped fields are `vregex` (compile-time DFA tables); codecs come from `@reflect` codegen |
| `sync.Mutex` around shared state | No shared state on constrained paths — seqlocks and wait-free rings at verified synchronization points only; lock-order violations fail compilation |
| GC ("let it handle memory") | **The big shift:** data stacks and pools with visible, bounded costs. You must know where every byte lives and dies |
| Nil-safe `return nil, err` patterns | No nil-tolerance anywhere; absence is `vvariant`, errors are typed discriminants |

### Unlearn These Habits

- **"Allocation is free; the GC cleans up."** Every allocation is an effect (`@alloc`) that a constrained context forbids.
- **"Just spawn a goroutine for that."** Concurrency is designed at build time, not reached for at run time.
- **"An interface is the contract; I'll discover the type at runtime."** Contracts are `@pre`/`@post` proven before the binary exists; types are sealed at link time.
- **"Parse JSON generically."** Structure is declared, packed, and asserted — dynamic schemas do not exist.

### First-Week Compile Errors

A `go`-style spawn; sending a non-flat value through a channel; a dynamic-dispatch call on an unsealed type; an `@alloc` inside a `@nolatency` loop; a runtime reflection query.

---

## 🧩 A Note for Zig Developers (Bonus)

You were not on the original list, but you'll recognize VeriObjC faster than anyone: explicit allocators, `comptime` ≈ `vconstexpr` (with the addition of fuel and budgets), no hidden control flow, C ABI first. Your adjustments: compile-time evaluation is *budgeted* rather than unbounded, generics are constrained rather than duck-typed, and the closed world means no comptime-codegen surprises from another translation unit. Read the C++ section for the layout discipline and the Rust section for the ownership taxonomy.