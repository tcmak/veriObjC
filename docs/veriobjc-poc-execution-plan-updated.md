# 📋 VeriObjC Execution Plan: Proof of Concept (PoC) Implementation

This document establishes the roadmap, task breakdown, strict acceptance criteria, and verification strategy for the two engine rooms of VeriObjC: the Clang Core Lowering &amp; Ownership Frontend Plugin and the Freestanding Closed-World Static Micro-Runtime.

---

## 🧭 0. Implementation Language Decision Record

No single language builds this project — the architecture forces a split, and for one layer the choice is made for us:

| Component | Language | Rationale |
| :--- | :--- | :--- |
| **Compiler frontend** (AST plugin, effect checker, flatness/ownership graphs, `vconstexpr` interpreter, all reports) | **C++** (LLVM/Clang dialect) | Forced: a Clang plugin is a C++ shared library linked against Clang/LLVM C++ APIs (`Sema`, `ASTMatchers`, `CodeGen`). SMACK and Z3's primary APIs are also C++, so the proof wiring stays in one language. |
| **Micro-runtime + `vstd`** (`rt/`, data stacks, `vpool`, rings, tape, channels) | **C** | Freestanding, auditable by a single careful reader, and compiled by the very Clang pipeline it serves — the runtime dogfoods its own language's memory rules. ABI-identical to the `@export` FFI surface. |
| **Package manager (`vpm`)**, content-addressed store, manifest checking, CI report diffing | **Go** | Bootstrap independence: `vpm` downloads, verifies, and pins the toolchain, so it must not depend on it. A single static, boringly-stable binary that runs anywhere without dragging LLVM. |
| **Type/effect rule prototyping** (optional, throwaway) | **OCaml or Haskell** | Fast iteration on constraint-checking rules before porting them into the plugin. Never ships. |

**Rejected:**

- **Traditional ObjC** — building the escape hatch out of the thing being escaped (libobjc, message sends, runtime dependency).
- **Rust for the frontend or `vpm`** — the plugin API is C++; and rustc pins its own LLVM version, creating skew against our pinned Clang/LLVM. A self-inflicted bootstrap headache.
- **Haskell/Go for production tooling beyond prototyping** — poor ops story for tools installed on trading desks and CI runners (Go is *approved* for `vpm`, which is not a constrained path).
- **Zig for `vpm`** — philosophically closest (explicit allocators, `comptime` ≈ fueled `vconstexpr`) and it bundles a C toolchain, an elegant bootstrap story; deferred only because it remains pre-1.0 with breaking changes between minors. Revisit if the bundled-toolchain bootstrap becomes a product feature.

**Principle:** minimize independent moving parts — two toolchain-grade languages (one forced, one chosen for the substrate it targets) plus one boring utility binary.

**Self-hosting endpoint:** the `vconstexpr` interpreter is already an interpreter for a VeriObjC subset; rewriting the runtime in VeriObjC itself is the natural final milestone (the same path C compilers took) and is explicitly out of scope for this PoC.

---

## 🛠️ Module 1: Clang Ownership State Machine (Compiler Plugin)

The frontend plugin must statically intercept the Abstract Syntax Tree (AST), block banned dynamic language features, map code contract assertions straight to our formal math engine, and aggressively lower Objective-C syntax directly into static C data networks.

### 📌 Task List

1. **Boilerplate &amp; AST Registration:** Set up a `PluginASTAction` infrastructure and register a custom `ASTConsumer` inside Clang. This architecture hooks directly into the frontend pipeline immediately after parsing, allowing VeriObjC to parse and mutate custom attributes before standard LLVM IR generation occurs.
2. **Strict Banned Filter Pass:** Implement an AST Visitor that matches and instantly aborts compilation upon encountering the `id` keyword, `NSObject` inheritance chains, `nil` messaging on unproven pointers, any runtime reflection hooks, or any escaping store of a function block.
3. **Linear Type Ownership State Tracker:** Build an AST analyzer path that enforces single-ownership tracking for contiguous memory frames, tracking states from allocation through the execution pipeline (`Unallocated -> Active -> Consumed/Dead`). Extend the tracker to cover **data-stack roots**: every reference is statically rooted at the scope whose data stack bounds its target, and a reference may not be stored anywhere that outlives its root. Growth on a data stack never invalidates references (stable-reference guarantee).
4. **Design-by-Contract Formula Extractor:** Intercept `@pre()` and `@post()` method tokens and translate them into pure logical boundary equations mapped directly into intermediate downstream files for the `SMACK / Z3` formal proof solver.
5. **Range-Based Loop Expander:** Intercept the custom `for (auto &x : array)` syntax inside Objective-C interface scopes. The plugin evaluates the container's compile-time capacity boundaries and lowers the block directly into an indexed raw C pointer loop (`ForStmt`), enabling effortless hardware vectorization (SIMD) or clean downstream optimization.
6. **Nominal Devirtualization Engine:** Evaluate all call sites. If a receiver's type is explicitly known or marked `@final`, lower the square-bracket messaging token into a flat direct C-function invocation.
7. **Compile-Time Reflection &amp; SoA Transmuter:** Process metadata loops tagged with `__attribute__((objc_soa(Type, N)))` or `@reflect()`. Compute raw field byte alignments during the compilation phase, generating flat C arrays that completely bypass runtime lookup tables.
8. **Multi-Frame Error Return Lowering:** Lower `return err from f` expressions into the innermost active `f` frame: statically verify every intervening call site lies inside an `f` call, propagate the typed error discriminant per frame (a hidden flag, not an unwinder), and reject any uncaught `from` at the top level.
9. **Function Block Lowering:** Lower function blocks (`xs.filter() { it % 2 == 0 }`) into monomorphized, non-escaping function values; any store of a block into a heap/escaping location is a compile error. A block's `return` binds to its lexically enclosing function.
10. **Flat-Value Channel Enforcement:** For every `vatomic_channel` send, verify the payload type is flat (no references at any depth, no `@shared` objects, no data-stack references); non-flat sends abort compilation.
11. **Built-in-Place Construction Verification:** Verify no intermediate copies are introduced for construction chains (`push(parse(...))`, returning growable arrays by value); the destination slot must be threaded down as an out-parameter at the IR level.
12. **Generic Constraint Checking at Definition:** `vgeneric` bodies typecheck exactly once against their declared constraints at the definition site; instantiation can never produce new errors. Unconstrained type parameters, specialization-as-metaprogramming, and SFINAE-style probing abort compilation. Run `vconstexpr` bodies in a fuel-bound interpreter (max steps / recursion depth / result allocation) — exhaustion is a clean error naming function and line, never a hang or OOM.
13. **Monomorphization Budget &amp; Reporting:** Deduplicate instantiations program-wide by mangled key; enforce per-package instantiation and generated-code budgets declared in the manifest; emit `--mono-report` (instantiation count, code bytes, strategy `@mono`/`@erased`, top consumers). Exceeding a budget fails the build naming the package and generic.
14. **`vvariant` Exhaustiveness &amp; Misprediction Audit:** Lower `match` to link-time jump tables with compile-time exhaustiveness checking. Implement `--branch-report`: classify data-dependent branches in `@nolatency` contexts as proven (contract-derived), hinted (`@likely`/`@unlikely`), or unproven, and list the unproven ones per line.
15. **FFI Effect &amp; Boundary Checking:** Every `extern fn` must declare its effects (`@io`, `@blocking`, `@alloc`, `@fence`, `@shared-retain`); an unannotated foreign function defaults to `@cold` and is uncallable from `@nolatency`/`@pure` contexts. Verify every value crossing the FFI boundary is flat or an opaque `vpool` handle, that exported struct layouts are pinned (`__packed`/`__align` + `vstatic_assert`), and that no allocator mixing occurs — data-stack/pool memory never reaches a foreign `free`, and foreign allocations never enter VeriObjC recycling. `@export` functions generate a C header carrying their `@pre`/`@post` as documented preconditions.
16. **Compile-Time Regex (`vregex`) to DFA:** Lower `vregex<Pattern, Cap>` inside the fueled `vconstexpr` interpreter: parse → NFA → subset construction → Hopcroft minimization → flat `consteval` DFA table in `.rodata`, memoized program-wide. Reject backreferences/lookaround with clean errors naming the pattern and a suggested rewrite; budget group nesting (depth 8) and table bytes against the manifest's generated-data budget. Lower `.match()` to a fixed-trip table walk over a `@pre`-bounded input — one table load per byte, zero allocation, no data-dependent branches in the loop. Captures only for one-pass patterns (compile-time check against the minimized DFA), emitted as flat `(start, end)` offsets.

### ✅ Acceptance Criteria

- **Zero-Bypass Enforcement:** If a file contains the keyword `id`, compilation **must** instantly halt with code `1` and emit a custom error message.
- **Compile-time Failure on Double Free / Use-After-Free:** If a variable marked `consumed` is referenced on any subsequent execution line, compilation fails automatically.
- **Reference-Root Safety:** Storing a data-stack reference into a longer-lived destination produces a compile error naming the root and destination (Goose-style diagnostics).
- **Escape Rejection:** A function block stored into an escaping location, or a non-flat value sent through a channel, fails compilation.
- **Error-Return Coverage:** An uncaught `return err from f` (a call of the inner function outside any `f` frame) is a static error.
- **Perfect Lowering:** The AST output generated by the plugin must emit flat C functions without any trace of `objc_msgSend` square-bracket syntax.
- **No Compiler Explosion:** A deliberately pathological generic (deeply nested parameterized containers with many distinct type arguments) must fail with a budget- or fuel-exceeded error naming the package and generic — never an out-of-memory, never a hang.
- **Definition-Site Errors:** An invalid generic body errors at its definition, not at its first (or fiftieth) instantiation.
- **Exhaustive Matches:** A `match` missing a case fails to compile; adding a new case to a `vvariant` fails every non-exhaustive `match` downstream.
- **Branch Survivor Report:** `--branch-report` must classify a contract-proven one-sided branch as proven and list genuinely data-dependent branches as unproven.
- **FFI Discipline:** An `extern fn` without declared effects is treated as `@cold`; calling it from a `@nolatency` context fails compilation. Handing a data-stack reference or pool slot across the FFI boundary fails compilation.
- **Regex Worst Case Is a Theorem:** A `vregex` match over a `@pre`-bounded input executes exactly `cap` table loads with an empty `--effects-report` entry; a backreference/lookaround pattern fails to compile with a suggested rewrite; a state-explosion pattern fails the byte budget naming the pattern — never a hang, never an OOM.

### 🧪 Verification &amp; Testing Strategy: How Do We Know It Works?

- **Plugin Hook Verification:** Run the compiler toolchain passing `-Xclang -load -Xclang your_plugin.dylib -Xclang -plugin -Xclang veriobjc-lower`. Assert that the Clang compiler initializes without throwing binary mismatch errors, confirming the PluginASTAction boilerplate registers correctly.
- **Loop Expansion Analysis:** Pass a script containing a range-based loop over a `vvector` through the compiler. Run `clang -Xclang -ast-dump` and verify that the original range-based loop block has completely disappeared from the tree, fully lowered into a flat sequence of a pointer declaration (`VarDecl`), boundary comparison (`BinaryOperator <`), and index increment (`UnaryOperator ++`).
- **Negative Test Suite (Expected Failures):** Create a series of broken code snippets (`test_leak.m`, `test_use_after_free.m`, `test_id_usage.m`, `test_escaping_block.m`, `test_nonflat_channel_send.m`, `test_stale_root_ref.m`, `test_cold_ffi_call.m`, `test_ffi_stack_ref_export.m`, `test_backref_regex.m`, `test_regex_state_explosion.m`). The plugin passes if and only if **100% of these files fail to compile**.
- **Effects Survivor Report:** Compile a hot-loop benchmark with `--effects-report` and assert the report is empty for a provably effect-free loop and lists exactly the annotated `@io` sites for an I/O loop.
- **AST Dumper Analysis:** Run `clang -Xclang -ast-dump` with the plugin activated. Verify that the range loop expands directly into clear `ForStmt`, `VarDecl` (pointers), and explicit sequential pointer increments (`UnaryOperator ++`).

---

## ⚡ Module 2: The Freestanding Closed-World Micro-Runtime (`rt/`)

A microscopic, freestanding, operating-system-independent execution floor optimized to manage headerless `@owned` object layouts and resolve link-time perfect-hashed method jump tables without a global heap.

### 📌 Task List

1. **Headerless Object Contiguous Packing Layer:** Implement the compilation logic that structures `@owned` object layouts. Ensure instances pack as a sequential group of primitive components with zero prefix bytes allocated for dynamic runtime metadata or class pointers (`isa`), mapping identically to raw C struct layout alignments.
2. **Data-Stack Substrate &amp; Reusable Pools:** Implement the freestanding memory region manager. A data stack is a large address-space reservation with a bump pointer; the toolchain assigns N stacks statically per core and enforces at most one resizable value live on top of each. Growth advances the pointer and never moves data; **scope exit resets the top in a single store** (the free). Implement `vpool<Type>` as a grow-only array plus a hidden free-slot freelist: `allocRef` reuses a slot or pushes, `free` returns the slot, and pool memory is never released—so a stale slot reads a *different value of the correct type*, never corrupt memory. Named `@arena` regions bind data stacks to pre-mapped hardware windows (DMA, MMIO).
3. **Link-Time Perfect-Hashed Vtable Generator:** Replace dynamic method caching with a linker script infrastructure. It must hash the unique `uint32_t` selector identifiers for protocols and abstract interfaces, packing direct function pointers into immutable, conflict-free jump matrices inside read-only memory (`.rodata`).
4. **Deterministic Scoped Context Hook:** Implement the `VeriObjC_ScopeRelease` routine bound to the `@scoped` macro context using `__attribute__((cleanup))` to reset data-stack allocation windows en-masse upon block termination (one store per stack).
5. **Self-Relative Tape Format:** Implement `to_bytes` / `from_bytes` for self-relative (`__relative`) linked structures: emit framed byte images of data-stack arrays and pools, and verify framing, tags, lengths, and every link on load—rejecting corrupt or hostile bytes before they become references. This is the substrate for the tape log, replay captures, and order-book checkpoints.

### ✅ Acceptance Criteria

- **OS Independence:** The file must compile cleanly using `-ffreestanding` and `-nostdlib`, showing exactly zero imports from `libc`, `Foundation`, or traditional operating system layers.
- **Single contiguous allocation:** Creating an object containing a flexible array member must take precisely one continuous block memory slice.
- **Absolute O(1) Predictability:** Data-stack growth, scope reset, and pool `allocRef`/`free` must execute in completely bounded, identical CPU cycles without tracking a complex linked-list heap map.
- **One-Store Frees:** Resetting a scope owning a deeply nested million-element structure must emit a single store per data stack (verify via `llvm-objdump`).
- **Stable References:** A reference captured before a million subsequent pushes must remain writable afterward (runtime test).
- **Same-Typed Stale Reads:** Reading a freed `vpool` slot must return a value of the correct type (never memory corruption); the test asserts type-consistency, not value.
- **Verified Snapshots:** `from_bytes` must reject every mutated framing byte, tag, and link offset in a fuzz corpus (no wild references).
- **Zero Metaprogramming Allocation Tax:** Querying the `sizeof()` of an `@owned` object must match the exact sum of its instance fields, proving the complete elimination of object base-tax overhead.
- **Constant-Time Bound Dispatch:** Erased dispatch pathways through perfect-hashed matrices must resolve in identical, fixed clock loops without causing L1/L2 cache pollution or dynamic runtime lookups.

### 🧪 Verification &amp; Testing Strategy: How Do We Know It Works?

- **Bare-Metal Emulation (QEMU / Unit Host):** Compile the micro-runtime as a standalone static library. Link it against a test binary executing inside a minimal unit runner or sandboxed mock layer.
- **L1/L2 Cache Line Verification:** Compile a program that iterates through fields and flexible array members 10,000,000 times. Utilize micro-profiling tools to verify that cache line misses approach zero, proving contiguous alignment correctness.
- **Deterministic Clock Audit:** Run the code on a loop and sample executing durations via hardware cycle counters (`rdtsc` on x86 or `CNTVCT_EL0` on ARM). The runtime passes if the timing distribution contains absolutely zero outlier spikes.
- **Microarchitectural Profiling Loop:** Execute an operational loop 10,000,000 times across contiguous memory structures. Track hardware cycles using low-level cycle registers (`rdtsc` / `CNTVCT_EL0`) to prove that performance data trails show zero variance spikes or cache misses.
- **Pool Staleness Fuzz:** Randomly interleave `allocRef`/`free` with readers holding old references; assert no type confusion and no out-of-bounds access across millions of operations.

---

## 🔄 Integration Testing Pipeline

We verify the entire VeriObjC pipeline by setting up an end-to-end integration test runner:

1. **Step 1:** Pass a complete, valid `NetworkProcessor.m` script into the toolchain.
2. **Step 2:** Assert that the compiler plugin returns exit code `0` (Success).
3. **Step 3:** Use `llvm-objdump -d` to extract the compiled machine instructions. Visually audit that **no references to Apple's `objc_msgSend` exist**, that message loops have collapsed completely into flat direct assembly jumps, and that scope exits compile to single store instructions per data stack.
4. **Step 4:** Pass a corrupted file into the compiler plugin and ensure it is flatly rejected before generating any machine instructions.
5. **Step 5:** Round-trip an order-book checkpoint through `to_bytes`/`from_bytes`, tamper with random bytes, and assert every tampered load is rejected.

---

## 📈 Module 3: Measurement &amp; Latency CI ("No Number, No Claim")

As Amdahl's law stated, -O0 timings mean nothing, *knowing which code is hot* is the real skill — becomes infrastructure rather than culture.

### 📌 Task List

1. **Benchmark Harness:** Cycle-accurate (`rdtsc`/`CNTVCT_EL0`) microbenchmark runner for every `vstd` container and primitive, executing on pinned, isolated cores with pre-faulted, warmed memory. Golden results stored per reference machine.
2. **Latency Budget Gates:** Per-module p50/p99/p99.9 budgets committed alongside the source as versioned artifacts; CI fails on regression beyond measured noise bands. Budgets are reviewed like code.
3. **Report Diffs as CI Artifacts:** `--effects-report`, `--branch-report`, and `--mono-report` are published per build and diffed against the previous build. A certified hot loop that *gains* an effect, an unproven branch, or an instantiation fails review even when all tests pass.
4. **Benchmark Honesty:** Publish losses alongside wins (all machines, all configurations), and time whole processes including teardown, matching the training's measurement discipline.

### ✅ Acceptance Criteria

- A change that adds an `@alloc` call site to a certified loop cannot pass CI (the effects-report diff blocks it).
- A p99 regression beyond the noise band on a budgeted module fails the build, naming the offending commit.
- Every performance claim in the documentation cites a benchmark ID reproducible from CI artifacts.