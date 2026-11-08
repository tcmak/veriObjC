# 📚 VeriObjC Ecosystem: Foundational Library Architecture (`vstd`)

This specification details the baseline execution architecture and value types of the VeriObjC Core Library (`vstd`) and its embedded Micro-Runtime (`rt/`). It defines the total elimination of legacy dynamic runtime systems, replacing them with zero-cost, type-safe, and formally verified static structures.

---

## 🏛️ Core Design Philosophy

Every library and application layout included in the VeriObjC ecosystem must strictly adhere to four architectural pillars:

- **Stack-First Memory Model:** Program memory is the native call stack, static data, and N compiler-assigned grow-only **data stacks**. There is no general heap. Scope exit is the free (one store). References into growing data stay valid because growth never moves data. State that must be recycled or outlive its scope lives in `vpool` reusable pools—the only sanctioned recycling mechanism. `@shared` refcounted objects are quarantined to the `@cold` boundary and may never appear on constrained paths.
- **Zero Runtime Heap Allocation:** All foundational types and data structures map exclusively onto data stacks, static stack frames, or dedicated contiguous blocks initialized via hardware memory regions (`@arena`).
- **Absolute Determinism via Formal Proofs:** Underlying data models must be representable as finite state machines or linear graphs with strict, compile-time bounded limits. This enables our downstream mathematical solver (`SMACK / Z3`) to exhaustively sweep and verify bounded execution paths before machine code emission.
- **Nominal Exclusivity:** Absolute banishment of the raw dynamic `id` keyword ensures the Clang compiler plugin can resolve high-level abstractions (iterators, views, reflections) and compute explicit structural byte offsets statically at compile time via `constexpr`.

---

## The library set

| :--- | :--- | :--- | :--- |  
| sys0/ | freestanding substrate | (was: nothing - POSIX did this) |  
| rt/ | micro-objc runtime | (was: provided by Apple) |  
| core/ | language core libraries | (was: ll.core) |  
| par/ | deterministic parallelism | (was: ll.core.sync + framework) |  
| hw/ | device &amp; I/O | (was: ll.net + kernel) |

| :--- | :--- | :--- | :--- |  
| sys0 | = picolibc (or compiler-rt) subset | &lt;- adopt, don't reinvent |  
| | + your arch-specific pieces | &lt;- per-CPU areas, boot, MMU, DMA |  
| | + syscall layer | &lt;- deleted: effects system replaces it |  
| rt | clean room, zero libc dependencies | &lt;- linker-verified: no malloc/pthread refs |  
| core/par/hw | built on sys0 only | | |

## Effect system

The library design leans on compiler-level effect/ownership discipline (this is what 'zero-tolerance' means concretely):

- Every function is inferred/marked with effects: `@alloc`, `@shared-retain`, `@blocking`, `@io`, `@fence`
- Functions declare an effect budget, code in a @pure/@nolatency context that calls something with a forbidden effect = compile error, not a code-review comment
- Initialization is a phase in the type system: @init -context can call anything: post-init contexts provably can't allocate or touch devices - this replaces the strace tripwire with a proof.
- **Survivor reporting:** `--effects-report` lists every effectful call site that survives into a given context, per line of source—the audit surface for latency reviews (analogous to bounds-check reporting in constrained compilers).

---

## 🧬 1. The Micro-Runtime Layer (`rt/`)

The `rt/` subsystem completely replaces Apple's legacy `libobjc.dylib`. It removes all non-deterministic dynamic dispatch, runtime string lookups, and arbitrary pointer chasing, substituting a **closed-world compilation model**.

### 🏛️ Link-Time Sealed World

- **Static Layouts:** All classes, protocols, methods, and selectors must be fully declared and resolved at link time. The compiler and linker construct static class layout tables directly inside immutable read-only memory (`.rodata`). Runtime class registration, dynamic category loading, and method swizzling are strictly forbidden and emit compile-time errors.
- **Selector Interning:** The traditional `@selector(name:)` mechanism is stripped of string lookup tables. The custom Clang plugin assigns a unique, static `uint32_t` integer identifier to every unique selector string during the parsing phase.

### ⚡ Zero-Overhead Dispatch Engine

Traditional `objc_msgSend` caching is completely eliminated. Subsystem method resolution follows a strict hierarchy of static optimization:

\[Receiver message\]  
│  
├── 1. Statically Known / @final Type? ──► Direct Inline C-Call (Optimizer)  
│  
└── 2. Erased Base Protocol Interface? ──► Link-Time Perfect-Hashed Jump Table (O(1))

- **Direct Devirtualisation:** If the receiver's type is statically known or explicitly marked `@final`, the compiler rewrites the bracket message directly into a fast, inlineable C-style function call.
- **Perfect-Hashed Jump Tables:** When sending a message to a structurally erased protocol interface type, the runtime routes invocation through a per-class virtual table. This table is perfect-hashed at link time using the selector's `uint32_t` identifier. Dispatch is strictly O(1), deterministic, and touches no shared dynamic cache lines.
- **Strictly Typed `nil` Messaging:** Objective-C's traditional safety remains, but it is explicitly typed. Pointers must be decorated with `@optional`. Sending messages to standard pointers without checking is a compile-time fault unless wrapped in a branch condition.

---

## 🏷️ Compiler-Enforced &amp; Hint Attributes

- **Inspired by:** C++ `[[nodiscard]]`, `[[deprecated]]`, `[[likely]]`/`[[unlikely]]`, `[[maybe_unused]]` .
- **Enforced:** `@nodiscard` — dropping the result of a marked method (a `vresult`, a generation check, a `vweak` refresh) is a compile error. `@deprecated("use X")` — usage warns with migration text. `@maybe_unused` — suppresses unused warnings without deleting code.
- **Hints:** `@likely` / `@unlikely` on branches — the compiler may use them for layout and prediction-friendly codegen, and `--branch-report` (advanced spec §5) flags where a hint disagrees with contract-derived knowledge.
- **Rationale:** The cheapest safety wins in the dialect — pure compile-time checks, zero runtime cost, encoding review comments ("did you check that return?") into the toolchain.

---

## 📦 2. Text &amp; Memory Windows

### 📝 Immutable Strings: `vstring_view`

- **Inspired by:** `std::string_view` (C++17) and Rust string slices (`&str`).
- **Structure:** A flat, stack-passed structure containing an explicit non-null read-only char pointer and an explicit `size_t` boundary length.
- **Behavior:** Supports compile-time metadata processing via C23 `constexpr`. No hidden memory allocations or null-terminator parsing overhead.

### 🏷️ Bounded Text Buffers: `vfixed_string(N)`

- **Inspired by:** Stack-bound embedded string structures.
- **Structure:** A contiguous array of exact layout size `N` encapsulated within a sealed Objective-C object structure.
- **Behavior:** Completely eliminates out-of-bound overflow vulnerabilities. If the compiler's linear analysis finds an execution path that writes characters exceeding `N`, compilation aborts.

### 🔍 Contiguous Slices: `vspan<Type>`

- **Inspired by:** `std::span` (C++20).
- **Structure:** A bounds-checked sliding window over memory-mapped physical registers, DMA blocks, or stack arrays. It carries zero allocation cost, operating simply as a safe pointer-boundary pair.

### 🏗️ Grow-Only Stack Arrays: `vstack_array<Type>`

- **Inspired by:** Goose data stacks.
- **Structure:** One resizable value lives on top of a compiler-assigned data stack; growth advances a bump pointer and never moves data. At most one growable value is live per data stack (compiler-enforced).
- **Behavior:** `push` returns a reference that stays valid across all later growth. Scope exit resets the stack top in a single store—freeing a million-element structure costs one instruction, however deeply it nests.

---

## 🗃️ 3. Cache-Deterministic Collections

### 📊 Inplace Containers: `vvector<Type, N>`

- **Inspired by:** `std::inplace_vector` (C++26 P0843) and Rust's `FixedVec`.
- **The Concept:** Standard dynamic vectors require dynamic memory allocation. When capacity is exceeded, they reallocate on the heap, destroying real-time predictability. vvector guarantees zero-fragmentation.
- **Structure:** Elements sit directly inside the object's instance variable storage footprint block. The struct retains an active `uint32_t` counter element.
- **Behavior:** Replaces heap-growing `NSArray`. If a process attempts to `push_back()` an element beyond the static boundary `N`, the mathematical prover triggers a compiler error.

### 🔄 Reusable Pools: `vpool<Type>`

- **Inspired by:** Goose `reusable` pools.
- **The Concept:** Data that must outlive its constructing scope, or be recycled—order slots, packet contexts, connection state, arena-crossing objects—lives in a pool: a grow-only array paired with a hidden freelist. Pools are the *only* sanctioned recycling mechanism; they replace malloc/free pairs, object caches, and hand-rolled freelists.
- **Structure:** One contiguous grow-only array plus a free-slot index stack. `allocRef` reuses a freed slot or pushes; `free` hands the slot back.
- **Behavior:** Since pool memory is never actually released, a stale reference reads a *different value of the correct type*—a logic bug, never memory corruption. Pools are passed explicitly into recursion and cross-scope code, keeping the data-stack discipline intact.

### 🗺️ Flat Map: `vflat_map<Key, Value, N>`

- **Inspired by:** `std::flat_map` (C++23).
- **Structure:** Two inline, parallel sequential arrays arranged directly for linear memory storage.
- **Behavior:** Replaces the pointer-chasing dictionary tables of `NSDictionary`. Elements are sorted in-place, allowing rapid binary search loops optimized perfectly for CPU cache hits.

### 🌀 Flat Hash Map: `vhash_flat<Key, Value, N>`

- **Inspired by:** abseil (`abs::flat_hash_map`) over `std::unordered_map`.
- **Structure:** Open addressing, power-of-two capacity, bitmap occupancy; keys and values in parallel contiguous arrays. No chaining, no per-node allocation, no pointer chasing.
- **Behavior:** Lookup is mask → probe → compare with a perfectly predictable miss path. Unlike `vflat_map` (sorted, binary search — better for iteration and static tables), `vhash_flat` is the hot-path insert/lookup default. Open addressing sells reference stability for layout; the data stacks buy stability back for arrays, and stored hash values should be pool indexes, not references.

### 🎴 Sum Types: `vvariant<T...>`

- **Inspired by:** `std::variant` + `std::visit` (C++17) and Goose's two-mode enums.
- **Structure:** A closed, named set of cases with inline payloads — a value-typed alternative to class hierarchies for closed sets (order states, message types, venue dispatch).
- **Behavior:** Exhaustive `match` compiles to a link-time jump table; a missing case is a compile error. Two storage modes at the point of use: fixed mode (tag + largest payload, indexable, overwritable) and packed mode (each value sized to its variant, cache-streaming). Cross-product matches are budgeted under the monomorphization guard (`vmeta` §3).

### 📦 Flat Values (definition)

- A **flat value** is contiguous, with no references at any depth: primitives, fixed arrays, inline strings, and structs thereof. Flatness is decidable at compile time by the Clang plugin and is the required shape for anything crossing a core boundary (see §5).

### 🛡️ Type-Safe Errors: `vresult<Type, ErrorCode>` + Multi-Frame Return

- **Inspired by:** Rust's `Result<T, E>` and Goose's `return err from f`.
- **Structure:** A C11 anonymous variant union packed directly inside an Objective-C instance signature, used at API boundaries.
- **Behavior:** Completely replaces standard Apple `@try / @catch` runtime exception unwinding. The Clang compiler plugin enforces a rule: a programmer cannot extract the raw `.value` payload unless the compiler graph can prove they checked the `.isSuccess` state branch first. For deep call chains, `return err("...") from f` returns the error as the result of the innermost active call of `f`, however many frames up—statically checked, typed via the `from` clause, with no unwinder and no `vresult` in intermediate signatures.

---

## 🔀 4. Flat Values &amp; Share-Nothing Channels

- **The rule:** Values crossing a `vatomic_channel` or any core boundary must be **flat** (no references at any depth). Crossing is a `memcpy`—which is cheap precisely because flat values are contiguous and carry real data inline.
- **Enforcement:** The Linear Ownership Graph verifies flatness at compile time; sending a non-flat value, or any `@shared` object, through a channel is a compile error.
- **Consequence:** Data races, locks, and refcounted sharing across cores are structurally absent rather than merely discouraged. `@shared` refcounting is quarantined to the `@cold` boundary and never crosses cores.

---

## 🤝 Borrow, Sink &amp; Weak References

- **Inspired by:** the C++ sink/borrow/share discipline — promoted to a checked part of the type system (`vmeta` §6).
- **Sink (`move`):** Passing `move(x)` transfers ownership; the Linear Ownership Graph proves the caller never touches `x` again.
- **Borrow (`borrow`, the default for non-trivial parameters):** Non-owning, rooted at the caller's scope, and provably non-escaping — storing a borrow where it outlives its root is a compile error naming root and destination.
- **Weak (`vweak<T>`):** A generation-counted pool handle. `allocRef`/`free` bump the slot generation, so a stale `vweak` reports *stale* instead of silently reading the next owner's data — detection, not same-typed garbage. `@shared` refcounting stays quarantined in `@cold`.

---

## 🧾 5. Self-Relative Tape &amp; Snapshot Format

- **Structure:** Links may be stored as narrow offsets (`__relative(8|16|32)`) instead of pointers, making structures **position independent**: they mean the same thing wherever they sit in memory, on disk, or on the wire.
- **Serialization is the data layout:** `to_bytes` is a write; `from_bytes` is a read plus a verification pass that checks framing, tags, lengths, and every link before bytes become references—corrupt or hostile input is a `false`, never a wild pointer.
- **Applied to:** the `core.tape` binary log, replay captures, market-data snapshots, order-book checkpoints, and any persisted state. This eliminates the serializer/schema module entirely: one representation serves in-memory state, persistence, and wire snapshots.

---

## 🧱 6. Built-in-Place Construction Guarantee

- **The rule:** A constructed value is always built at its final destination, propagated top-down through any depth of calls. `items.push(parse(line))` writes the parsed record directly into the array; returning a growable array by value costs nothing.
- **Consequence:** No temporaries, no defensive copies, no hidden moves. Out-parameters mostly disappear, and the copy elision guarantee is a language invariant rather than an optimizer's best effort.

---

## 🔬 7. Built-In Formal Testing &amp; Compile-Time Mocking

### 📐 Property-Based Proving: `vtest`

- **Mechanism:** Bypasses legacy unit tests that assert against static runtime inputs.
- **Behavior:** Test functions are declared alongside specific boundary preconditions via `@verify()`. The toolchain pipes these parameters directly into the **Z3 SMT Solver**, running a full mathematical boundary sweep. Verification passes only if the prover demonstrates that the underlying functions can never reach an invalid execution block across *all* valid parameter configurations.

### 🎭 Zero-Cost Injection: `__attribute__((objc_mock_for))`

- **Mechanism:** Eliminates dynamic proxy injections and method swizzling, which are strictly banned in VeriObjC.
- **Behavior:** Test variants are sealed against their targets at build time. When generating a test build binary, the Clang plugin scans for the class interface and automatically redirects the sealed dispatch switch matrix, swapping hardware execution paths for mock pipelines with zero runtime performance cost.

---

## 🌉 8. FFI & Interoperability

VeriObjC speaks the **C ABI in both directions**. It is the one boundary where the closed world opens, so the boundary itself is typed, checked, and reported like everything else.

### 📤 Exporting to Other Languages

- **`@export` functions** compile to plain C symbols with flat signatures: primitives, flat structs, `vspan`, or opaque handles. No `objc_msgSend`, no runtime — a generated C header is the entire integration surface.
- **Opaque handles:** Objects that cannot be flattened cross as `vpool` indices wrapped in generation-checked handles. The foreign side holds an opaque `uint64_t` and calls back through `@export` accessors; foreign code can never forge a VeriObjC reference.
- **Pinned layouts:** Exported structs must carry `__packed` / `__align` plus a `vstatic_assert(sizeof(...) == ...)` lock, making layout part of the API contract and CI-checked.
- **Guarantees don't transfer:** The solver proves *VeriObjC's* bounded paths, not the caller's. An exported function's `@pre`/`@post` become documented preconditions in the generated header — the C world gets checked calls, not proofs.

### 📥 Consuming Foreign Code (`extern fn`)

| Language    | Support level                                                                  | Boundary discipline                          |
| :---------- | :----------------------------------------------------------------------------- | :------------------------------------------- |
| **C**       | First-class — Objective-C is a C superset; existing headers import directly     | `extern fn` declares effects; flat values only |
| **Zig**     | Near-first-class — `@cImport` of the generated header; native C ABI consumers     | Same rules as C                              |
| **Rust**    | Via C ABI — `extern "C"` bindings to the generated header                        | No panics across the line; errors return as codes |
| **C++**     | Wrapper-only — `extern "C"` shims; no exceptions, templates, or classes cross    | Same rules; wrappers live in `@cold` or `@init` |
| **Go**      | `@cold`-only — cgo overhead and GC disqualify it from constrained paths          | Callable only from `@cold`/`@init` contexts  |

### 🚧 Boundary Rules (Compiler-Enforced)

- **Effects are declared, or the function is cold:** Every `extern fn` must list its effects (`@io`, `@blocking`, `@alloc`, `@fence`, `@shared-retain`). An unannotated foreign function **defaults to `@cold`** and cannot be called from `@nolatency`/`@pure` contexts — foreign memory behavior is unprovable, so honesty is mandatory and conservative.
- **Flat values only, both directions:** Anything crossing must be flat or an opaque handle — the same rule as channels and core boundaries, enforced by the same flatness check.
- **No allocator mixing:** Memory from foreign allocators is never freed by `vpool`/data-stack machinery, and VeriObjC memory never reaches a foreign `free`. Each allocator's products stay on its side of the line.
- **Reported like everything else:** Every surviving `extern fn` call site appears in `--effects-report` per line, exactly like native effectful calls.

---

## 📋 Comprehensive System Mapping Matrix


| Banned Legacy Framework           | Modern Inspiration                       | VeriObjC Ecosystem Component             | Core Low-Level Advantage                                                                                    |
| :--------------------------------- | :---------------------------------------- | :---------------------------------------- | :----------------------------------------------------------------------------------------------------------- |
| `NSString`                        | C++17 `string_view`                      | `<vstd/string.h>`                        | **Zero Allocation** immutable text views.                                                                   |
| `NSArray` / `NSSet`               | Rust `FixedVec`                          | `<vstd/vector.h>`                        | Statically bound, cache-friendly array layouts.                                                             |
| Growable arrays / `std::vector`   | Goose data stacks                        | `<vstd/stack.h>`                         | Grow-only bump allocation; **references never move**; free = one store.                                     |
| `NSCache` / hand-rolled freelists | Goose `reusable` pools                   | `<vstd/pool.h>`                          | Recycled slots read same-typed values, never corrupt memory.                                                |
| `NSDictionary`                    | C++23 `flat_map`                         | `<vstd/map.h>`                           | Linear memory lookup, zero pointer chasing.                                                                 |
| `@try` / `@catch`                 | Rust `Result<T, E>` + Goose error return | `<vstd/result.h>`                        | **No Stack Unwinding**. Typed errors via branch proofs and checked multi-frame returns.                     |
| GCD / `NSThread`                  | C++26 `std::execution`                   | `<vstd/execution.h>`                     | Bounded, lock-free structured state pipelines.                                                              |
| Runtime Mirroring                 | C++26 `std::meta`                        | `<vstd/reflect.h>`                       | **Pure Compile-time Metadata** injection. Zero runtime overhead.                                            |
| `^` blocks / closures             | Goose function blocks                    | `<vstd/blocks.h>`                        | Compile-time, non-escaping, zero allocation.                                                                |
| Serialization frameworks          | Goose self-relative links                | `<vstd/tape.h>`                          | **Data layout is the file format**; verified loading, zero pointer fixups.                                  |
| `std::unordered_map` / `HashMap`  | abseil `flat_hash_map`                   | `<vstd/hash.h>`                          | Open addressing, power-of-two probing, no chaining, predictable miss path.                                  |
| `std::variant` + `std::visit`     | Goose two-mode enums                     | `<vstd/variant.h>`                       | Exhaustive checked `match`; jump-table dispatch; packed or fixed storage.                                   |
| Smart-pointer sink/borrow/share   | Goose ownership + generation handles     | `move` / `borrow` / `vweak`              | Ownership taxonomy checked at compile time; weak reads detect staleness.                                    |
| `[[nodiscard]]` / `[[likely]]`    | C++ attributes                           | `@nodiscard` / `@deprecated` / `@likely` | Compile-time enforcement; zero runtime cost.                                                                |
| C++ templates                     | Rust generics + Goose bounded compute    | `vgeneric` + `vconstexpr` (`vmeta`)      | Definition-site checked generics; fueled interpreter; **budgeted instantiation — the compiler cannot OOM**. |
| Foreign-language interop          | C ABI + effect-declared `extern fn`      | `@export` / `extern fn` (`<vstd/ffi.h>`) | Flat values cross both directions; unannotated FFI defaults to `@cold`; no allocator mixing.               |
| Ternary LLM inference kernels     | BitNet b1.58 / MatMul-free LM           | `veriStream` (`ll.ml/`, `<vstd/stream.h>`) | `Bits<W>` wires + `Stream<T,N>` per-clock flows; `ternary_select` lowers to MUX + adder trees + popcount — **no multipliers, no DSPs, pure LUT logic**. |