# 🗺️ VeriObjC Master Architectural Library Mapping Matrix

This document establishes the definitive boundary line for the VeriObjC Standard Library (`vstd`) ecosystem by auditing the domain surface area of the **C++ Standard Template Library (STL)**, **Boost**, and the **Java Development Kit (JDK)**.

Every library domain is mapped into one of four rigid execution categories based on VeriObjC's bare-metal, zero-heap, and formally verifiable constraints.

---

## ❌ 1. The Forbidden Zone (Completely Banned)
These library domains are **completely excluded** from the VeriObjC language dialect. The custom Clang compiler plugin intercepts these patterns during the parsing phase and immediately aborts compilation to prevent non-deterministic or unsafe machine-code generation.

*   **Regular Expressions (`<regex>`, `boost::regex`, `java.util.regex`):** Highly non-deterministic execution paths, severe risks of catastrophic backtracking hangs, and complex runtime heap state-graph generation.
*   **Runtime Reflection & Introspection (`java.lang.reflect`):** Dynamic string-to-object queries and mutable class metadata manipulation break compile-time control-flow analysis and prevent mathematical verification.
*   **Dynamic Class Loading & Plugins (`java.lang.ClassLoader`):** Injecting or modifying executable code paths at runtime completely invalidates the static environment required for structural verification.
*   **Desktop UI Frameworks & Windowing (`java.awt`, `javax.swing`):** Reliant on heavy, unpredictable OS event managers, thread polling hooks, and vast layout allocation heaps.
*   **Database Connectivity (JDBC):** High-latency blocking I/O, unverified string generation, and non-deterministic remote network tracking.
*   **Escaping Closures / Runtime Blocks (`^` blocks that escape, `std::function`):** Hidden heap allocations and indirect calls that defeat inlining and effect analysis.

---

## 🟢 2. Pass-Through Zone (Zero Implementation Effort)
You do **not** need to develop custom implementations for these domains. They are either naturally deterministic, purely functional, or compile down immediately to low-level hardware or compiler operations.

*   **Math & Numerics (`<cmath>`, `<complex>`, `java.lang.Math`):** Trigonometric, algebraic, and bitwise arithmetic operations possess no internal state or hidden heap dependencies. VeriObjC routes these directly to LLVM compiler primitives (e.g., `__builtin_sinf`).
*   **Type Traits & Compile-time Limits (`<type_traits>`, `<limits>`):** Metadata systems evaluated strictly during compilation to measure type sizes, structural alignment boundaries, and mathematical type limits.
*   **Memory Barriers & Atomic Fences (`<atomic>` primitives):** The foundational hardware commands for atomic loads, stores, and fences are natively generated via standard C26 compiler intrinsic mappings.

---

## 🤖 3. Toolchain-Generated Zone (Handled by the Clang Plugin)
You do not build software libraries for these domains. Instead, **the compiler plugin auto-synthesizes them** by completely rewriting your Objective-C syntax patterns into flat machine execution models before code emission.

*   **Coroutines & Futures (`<coroutine>`, `boost::asio::yield`, Java Virtual Threads):** The Clang plugin auto-transforms your asynchronous method loops (`vasync`) into compact, fixed-size structural state machines on the stack frame.
*   **Structured Concurrency (`std::execution` (C++26), Java Structured Task Scope):** The plugin flattens lazy execution blocks into an explicit, deterministic pipeline graph directly on the caller's stack frame, computing the Worst-Case Execution Time (WCET) at build time.
*   **Compile-Time Reflection (`std::meta` (C++26)):** Handled entirely by our `@reflect()` AST token analyzer, outputting static integer offsets instead of string-lookup metadata tables.
*   **Data-Stack Memory Management (Goose data stacks):** Grow-only per-scope scratch allocation, stable-reference tracking, and scope-exit frees are synthesized by the plugin—there is no allocator library on constrained paths. Copy elimination (built-in-place construction) is likewise a plugin guarantee, not a runtime feature.
*   **Non-Escaping Function Blocks (`<vstd/blocks.h>`, Goose function blocks):** Loop bodies, comparators, and `filter`/`fold` callbacks are monomorphized compile-time entities; the plugin rejects any block that escapes.
*   **Serialization & Pointer Fixups (FlatBuffers / protobuf / JDK serialization):** Eliminated as a domain: self-relative (`__relative`) links make the in-memory layout position-independent, so saving is a byte write and loading is a read plus a verification pass (`to_bytes` / `from_bytes`).

---

## 🛠️ 4. The Creation Zone (Libraries We Must Build)
This forms your **VeriObjC Standard Library (`vstd`)**. You will develop these modules from scratch as highly restricted, type-safe, cache-friendly, and stack-allocated abstractions.

### A. Containers & Data Structures
*   **Memory Windows (`<vstd/span.h>`):** Replaces `std::span`. Provides type-safe, zero-cost window viewing over raw memory-mapped hardware blocks or stack frames.
*   **Inplace Vectors (`<vstd/vector.h>`):** Replaces `std::vector` and `java.util.ArrayList`. Stores elements inline inside the object structural allocation to prevent heap fragmentation.
*   **Grow-Only Stack Arrays (`<vstd/stack.h>`):** Replaces heap-growing buffers (`std::vector::reserve`, `StringBuilder`). One resizable value per compiler-assigned data stack; growth is a pointer bump, **references never move**, scope exit is the free. Pairs with built-in-place construction so `push(parse(...))` stores the record directly.
*   **Reusable Pools (`<vstd/pool.h>`):** Replaces hand-rolled freelists, `NSCache`, and Java object pools. A grow-only array plus a hidden freelist; `allocRef`/`free` recycle slots without ever releasing memory, so stale slots read same-typed values, never corrupt memory. The only sanctioned mechanism for state that outlives its scope or crosses recursion.
*   **Cache-Aligned Lookup Tables (`<vstd/map.h>`):** Replaces `std::unordered_map` and `java.util.HashMap`. Uses parallel contiguous arrays optimized for L1/L2 cache binary searches.

### B. Strings & Text
*   **String Slices (`<vstd/string.h>`):** Replaces `std::string_view`. Passes non-null, immutable pointers paired with compile-time bounded length variables.
*   **Fixed Strings:** Replaces `std::string` and `java.lang.String`. Stack-allocated character arrays of a fixed size, completely immune to buffer overflows.

### C. Thread Safety & Communication
*   **Linear Ownership Guards:** Replaces `<mutex>`, `std::lock_guard`, and `java.util.concurrent.locks`. Enforces that data variables are physically inaccessible unless an atomic synchronization handle is actively checked.
*   **Lock-Free Channels (`<vstd/channel.h>`):** Replaces Java BlockingQueues and Boost Lockfree queues. Provides an absolute O(1) single-producer single-consumer ring buffer for transferring variable ownership safely between CPU cores. **Payloads must be flat values** (no references at any depth): crossing is a `memcpy`, and non-flat sends are compile errors.

### D. Freestanding System Wrappers
*   **Asynchronous Non-Blocking I/O (`<vstd/io.h>`):** Replaces Java NIO and Boost ASIO. Maps data streams directly to fixed-size hardware ring buffers (similar to Linux `io_uring`), bypassing thread blocking behaviors completely.
*   **Hardware Clock (`<vstd/time.h>`):** Replaces `<chrono>` and `java.time`. Discards time zones and leap seconds entirely. Operates strictly as a 64-bit unsigned integer counter reading CPU cycles directly from physical chip registers.

### E. Native Validation & Mocking
*   **Property-Based Contract Verifier (`<vstd/contracts.h>`):** Replaces traditional unit testing libraries (JUnit, GoogleTest) and assertions. Leverages `@pre()` and `@post()` macros to pass logic verification rules to the Z3 SMT solver at compile time.
*   **Effect Survivor Reporting (`--effects-report`):** Replaces manual latency audits and code-review checklists. Compiler-generated report of every effectful call site surviving in a context, per line.

### F. Persistence & Capture
*   **Self-Relative Tape (`<vstd/tape.h>`):** Replaces serializer frameworks and schema tools. Binary log records, replay captures, market-data snapshots, and order-book checkpoints are stored position-independent with `__relative` links; `from_bytes` verifies framing, tags, lengths, and links before bytes become references—hostile input is a `false`, never a wild pointer.

---

## 📊 Comprehensive Reference Matrix

| Library Domain / Subsystem | Sourced Reference | VeriObjC Status | Core Realization Method |
| :--- | :--- | :--- | :--- |
| **Regular Expressions** | `<regex>` / Java Regex | ❌ **Forbidden** | Blocked via Clang AST filter pass |
| **User Interfaces & Layout** | Java Swing / AWT | ❌ **Forbidden** | Blocked via Clang AST filter pass |
| **Escaping Closures** | ObjC `^` blocks / `std::function` | ❌ **Forbidden** | Blocked via Clang AST filter pass |
| **Math & Numerics** | `<cmath>` / `java.lang.Math` | 🟢 **Pass-Through** | Lowered directly to LLVM Intrinsics |
| **Type Limits & Metadata** | `<limits>` / `<type_traits>` | 🟢 **Pass-Through** | Resolved instantly at compile time |
| **Coroutines & Event Loops** | `<coroutine>` / Java V-Threads | 🤖 **Toolchain-Gen** | Auto-synthesized via Clang AST Rewrite |
| **Structured Concurrency** | C++26 `std::execution` | 🤖 **Toolchain-Gen** | Flattened into Static Stack Graphs |
| **Compile-Time Reflection** | C++26 `std::meta` | 🤖 **Toolchain-Gen** | Evaluated via `@reflect()` Tokens |
| **Scratch Allocation & Copy Elision** | Goose data stacks | 🤖 **Toolchain-Gen** | Plugin-managed data stacks; scope exit = free; built-in-place construction |
| **Closures / Callbacks** | Goose function blocks | 🤖 **Toolchain-Gen** | Monomorphized non-escaping blocks; escape = compile error |
| **Serialization / Wire Formats** | FlatBuffers / protobuf | 🤖 **Toolchain-Gen** | Self-relative links; verified `to_bytes`/`from_bytes`; no schema module |
| **Sequential Arrays** | `std::inplace_vector` / `ArrayList`| 🛠️ **We Must Create** | Implemented as `<vstd/vector.h>` |
| **Growable Scratch Buffers** | Goose `T[>..]` | 🛠️ **We Must Create** | Implemented as `<vstd/stack.h>` grow-only stack arrays |
| **Object Recycling / Pools** | Goose `reusable` pools | 🛠️ **We Must Create** | Implemented as `<vstd/pool.h>` freelist-over-array pools |
| **Associative Key Maps** | `std::flat_map` / `HashMap` | 🛠️ **We Must Create** | Implemented as `<vstd/map.h>` |
| **Thread Synchronization** | `std::lock_guard` / Mutex | 🛠️ **We Must Create** | Implemented as Linear Ownership Guards |
| **Asynchronous Streaming** | Boost ASIO / Java NIO | 🛠️ **We Must Create** | Implemented as `<vstd/io.h>` Ring Buffers |
| **Error Propagation** | Rust `Result` / Goose `return err from` | 🛠️ **We Must Create** | `vresult` at boundaries + typed multi-frame `return err from f` in deep chains |
| **System Validation** | GoogleTest / JUnit | 🛠️ **We Must Create** | Implemented as `<vstd/contracts.h>` Solver Pipes |
| **Latency Auditing** | Goose `--bce-lines` | 🛠️ **We Must Create** | Implemented as `--effects-report` survivor reporting |
| **Snapshots & Replay Capture** | FlatBuffers / JDK serialization | 🛠️ **We Must Create** | Implemented as `<vstd/tape.h>` self-relative tape |
| **Generic Programming** | C++ templates / Rust generics | 🛠️ **We Must Create** | Implemented as `vgeneric` constrained generics + fueled `vconstexpr` interpreter (`vmeta`) |
| **Compile-Time Dispatch Tables** | `std::variant` + `std::visit` | 🛠️ **We Must Create** | Implemented as `<vstd/variant.h>` exhaustive `match` jump tables |
| **Strong Typing** | Hand-rolled tag wrappers (`mpl::Tag`) | 🛠️ **We Must Create** | Implemented as `vstrong` newtypes with no implicit conversions |
| **Borrow / Ownership Taxonomy** | Smart-pointer conventions (sink/borrow/share) | 🛠️ **We Must Create** | Implemented as `move` / `borrow` / `vweak` generation-checked handles |
| **Instantiation Cost Control** | *(no C++ equivalent — the gap)* | 🛠️ **We Must Create** | Manifest-declared monomorphization budgets + `--mono-report`; compiler cannot OOM |
| **Microbenchmark Culture** | "No number, no claim" (a common HFT practice) | 🛠️ **We Must Create** | Implemented as the Measurement & Latency CI module (budget gates, report diffs) |
| **Ternary LLM Inference (FPGA)** | BitNet b1.58 / MatMul-free LM | 🛠️ **We Must Create** | Implemented as `veriStream` (`ll.ml/`): `Bits<W>` wires, `Stream<T,N>` per-clock flows, `ternary_select` lowered to MUX + adder trees + popcount; pure-LUT 1.58-bit inference, `--hw-report` budgets |