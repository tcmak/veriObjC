# ⚡ VeriObjC (Verifiable Objective-C) — Architectural Specification

A hardened, freestanding, and formally verifiable dialect of Objective-C designed for bare-metal systems, kernel development, massively parallel compute, and ultra-low-latency environments.

---

## 📌 Context & Motivation
Objective-C is structurally a strict superset of pure ISO C. This unique architectural layout means it possesses an uncompromised ability to access low-level hardware, manipulate raw byte arrays, and map memory-mapped I/O (MMIO)—all while maintaining a highly expressive, elegant object-oriented (OO) paradigm via message passing.

However, historically, Objective-C has been bound to heavy desktop or mobile operating systems. Its reliance on Apple's dynamic `libobjc` runtime introduces heap allocation overhead, reflection, and non-deterministic execution paths, making it historically impossible to use in mission-critical, bare-metal, or high-throughput real-time architectures.

---

## 🎯 The Goal
The goal of **VeriObjC** is to create a restricted, hardened dialect of Objective-C and a supporting toolchain that compiles down to the exact machine-code efficiency of optimized C, while guaranteeing **mathematical formal verification** for both memory safety and control-flow correctness on statically bounded paths.

VeriObjC enables object-oriented programming for systems that tolerate absolutely zero runtime volatility, including:
* **Operating System Kernels** (Bare-metal booting, zero-runtime dependencies)
* **Massively Parallel Compute** (CUDA/GPGPU pipelines via LLVM NVPTX)
* **Hardware Synthesizers** (FPGA High-Level Synthesis mapping classes directly to silicon)
* **Ultra-Low-Latency Engines** (High-Frequency Trading, real-time audio signal processing)

---

## ❌ The Gap
Existing toolchains and languages force developers to make trade-offs:
1. **Standard Objective-C / Swift:** Introduce unpredictable runtime "hiccups" due to dynamic dispatch (`objc_msgSend`), Automated Reference Counting (ARC) register churning, and non-deterministic heap allocations.
2. **C++:** Offers static OOP but introduces significant binary bloat, slower compile times, a massive language surface area, and complex formal verification constraints.
3. **Rust:** Provides excellent safety guarantees but completely abandons the familiar syntax, dynamic flexibility, and architectural conventions of the Objective-C/C ecosystem.

VeriObjC bridges this gap. It preserves Objective-C's OOP elegance but aggressively strips away runtime non-determinism at compile time to achieve absolute predictability.

---

## 🛠️ Architectural Realisation

VeriObjC is realized not by writing a new language from scratch, but by delivering a highly specialized **developer toolchain and micro-runtime**.

```
[ Pure VeriObjC Source ] ──> [ Clang AST Plugin ] ──> [ Formal Proof (SMACK/Z3) ] ──> [ Bare-Metal Binary ]
                                                        (Linked with micro-libobjc)
```

### 1. The Language: Subtraction & Annotations
VeriObjC restricts standard Objective-C by compiling with zero tolerance for features that break mathematical determinism.
* 🚫 **Banned:** The generic `id` type, `nil` messaging, dynamic method swizzling, runtime class reflection, and `NSClassFromString`. Everything is strictly and nominally typed.
* 📦 **Retained:** Full object-oriented syntax (`@interface`, `@implementation`, `[receiver message]`), dynamic polymorphism, encapsulation, and inheritance.
* 🏷️ **Added:** Custom compiler attributes (e.g., `__attribute__((objc_sealed))` and `__attribute__((consumed))`) to define closed class hierarchies and track data ownership.

### 2. The Toolchain: Clang AST Frontend Plugin
A customized Clang compiler plugin intercepts the code's Abstract Syntax Tree (AST) to perform two distinct static mathematical proof validations before lowering to LLVM Intermediate Representation (IR):
* **Control-Flow Verification (Sealed Dispatch Matrix):** The plugin seals all class hierarchies at compile time. It replaces open-ended dynamic dispatch loops with a bound, highly optimized jump table or switch matrix, proving mathematically that an "unhandled selector" crash is physically impossible.
* **Memory Safety Verification (Linear Ownership Graph):** The plugin enforces strict memory ownership rules on low-level structures and C99 Flexible Array Members. It proves at compile time that objects have single owners, entirely eliminating use-after-free bugs and memory leaks without requiring runtime ARC overhead.

### 3. The Micro-Runtime: Freestanding `libobjc`
VeriObjC completely abandons Apple's standard `libobjc.dylib`. In its place, the toolchain links against a **microscopic, freestanding runtime (hundreds of lines of pure C)**. Because the Clang plugin resolves object lookup and ownership layout at compile time, the micro-runtime contains zero thread locks, zero string tables, and zero complex caching—allowing it to boot directly on bare metal or run inside a GPU core.

---

## 💡 The Core Syntactic Inventions

To bridge the gap between elegant OO and bare-metal safety, VeriObjC introduces explicit dialect extensions overlaid on C23 and Objective-C structures:

### 1. Range-Based for Loops
* **Syntax:** `for (auto &point : pool->coordinates) { ... }`
* **Mechanism:** Intercepted at compile time. The Clang plugin evaluates the fixed-size bounds of the target data buffer and lowers the block to a fast, zero-overhead C pointer increment pipeline, enabling seamless loop vectorization (SIMD) or physical FPGA unrolling.

### 2. Explicit Pointer Lifetimes (`__non_null` and `__nullable`)
* **Syntax:** `MouseEvent * __non_null event`
* **Mechanism:** Eradicates the non-deterministic pathing introduced by standard Objective-C silent `nil` messaging. All pointers are non-nullable by default. If flagged as `__nullable`, the plugin mandates explicit compile-time conditional safety checks before letting any message dispatch execute.

### 3. Move Semantics (`move()` Keyword)
* **Syntax:** `[router routeEvent:move(clickEvent)];`
* **Mechanism:** Acts as a custom compiler intrinsic that updates the linear tracking graph inside the toolchain. Once a variable passes through `move()`, its status transitions to "consumed", causing any subsequent local use-after-free reading or writing steps to immediately trip a compiler error.

### 4. Bounded Error Tracking (`Result` Object) and Multi-Frame Error Return
* **Syntax (boundary):**
  ```objc
  auto result = [audioEngine openStream:channel];
  if (result.isSuccess) { auto stream = result.value; }
  ```
* **Syntax (deep chains):**
  ```objc
  guard ok else { return err("bad frame") from parse; }
  ```
* **Mechanism:** Replaces the heavy stack-unwinding architecture of `@try / @catch` blocks. At API boundaries, memory-efficient `Result` structs are used. Within deep call chains, `return err from f` returns the error as the result of the innermost active call of `f`, however many frames up—statically checked (every call site must provably lie inside an `f` call), with no unwinder and no `Result` pollution of intermediate signatures. The `from` clause is **typed**: it names the error type, keeping error flow inside the effect system.

### 5. Deterministic Memory Regions (`@arena` Data Stacks)
* **Syntax:**
  ```objc
  @arena(DMA_Hardware_Pool) { auto frame = [FrameBuffer allocateWithLength:4096]; }
  ```
* **Mechanism:** A program's memory is the native call stack, static data, and **N compiler-assigned grow-only data stacks** (large address-space reservations with a bump pointer; N is computed statically). There is no general heap. At most one resizable value is live per stack, always on top, so **growth advances a pointer and never moves data: references into a growing array stay valid for as long as the array lives**. Scope exit is the free—a single store resets the stack top. For state that must be recycled or outlive its scope, `vpool<Type>` reusable pools (a grow-only array plus a hidden freelist) are the only sanctioned mechanism: a stale slot reads a different value of the *correct type*—a logic bug, never memory corruption. Named arenas (`DMA_Hardware_Pool`) bind data stacks to pre-mapped regions such as DMA windows.

### 6. Built-in-Place Construction
* **Mechanism:** A constructed value is always written at its final destination, propagated top-down through any call depth. `items.push(parse(line))` writes the parsed record directly into the array; returning a growable array by value costs nothing. No temporaries, no defensive copies, no hidden moves.

### 7. Function Blocks (Non-Escaping Closures)
* **Syntax:** `auto evens = xs.filter() { it % 2 == 0 };`
* **Mechanism:** Replaces Objective-C `^` blocks on all constrained paths. Function blocks are compile-time entities: monomorphized, direct calls, inlinable, and provably non-escaping. There are no closure objects and no function pointers on hot paths. A block may `return` from its lexically enclosing function.

### 8. Self-Relative Links & Native Snapshots
* **Syntax:** `typedef struct { uint32_t key; Msg *left __relative(32); } Node;`
* **Mechanism:** Links may be stored as narrow (1/2/4-byte) offsets instead of pointers, making structures **position independent**: `to_bytes` is a write, and `from_bytes` is a read plus a verification pass that rejects corrupt or hostile bytes before they become references. Tape records, replay captures, market-data snapshots, and order-book checkpoints use one representation for memory, persistence, and wire—eliminating serializers and schema fixups.

---

## 📊 Summary of Architectural Shift

| Requirement | Standard Objective-C | VeriObjC Dialect | Overhead |
| :--- | :--- | :--- | :--- |
| **Method Resolution** | Runtime dynamic search (`objc_msgSend`) | Flattened compile-time static jump matrix | **Zero** |
| **Memory Tracking** | Runtime counters (ARC register loops) | Compile-time Linear Type Ownership Graphs | **Zero** |
| **Heap Discipline** | `malloc`/`free` on a general heap | Compiler-assigned data stacks + `vpool` freelist | **Zero** (scope exit = one store) |
| **Reference Stability** | `NSArray` reallocation invalidates pointers | Grow-only stacks: references never move | **Zero** |
| **Error Handling** | Heavy exception stack unwinding | Branch-proven `Result` + multi-frame `return err from f` | **Minimal** |
| **Closures** | Heap-allocated `^` blocks | Compile-time non-escaping function blocks | **Zero** |
| **Serialization** | External frameworks + pointer fixups | Self-relative links; verified `from_bytes` | **Zero (data layout is the format)** |
| **Memory Isolation** | Core Foundation Heap `malloc`/`free` | Scope-bound `@arena` data stacks | **Zero** |
| **Execution Proof** | Empirical runtime testing (Unproven) | SMT-Solver Math Verification (Z3 via SMACK) | **Zero (Compile time)** |