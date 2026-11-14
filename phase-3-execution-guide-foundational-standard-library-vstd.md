# 🧭 Phase 3 Execution Guide — `vstd/`

A task-by-task walkthrough for building the foundational standard library: what to build, the red tests to write first, the invariants to hold, the audits to run, and the traps to avoid. Advisory — your TDD judgment governs; this guide is the map, not the law.

---

## 📏 Standing Rules (apply to every task below)

1. **Red first, always.** No behavior lands without its failing test committed before the implementation.
2. **The audit trifecta** for every runtime piece: `llvm-cov` 100% region/function/branch · `nm` zero libc/malloc symbols · `llvm-objdump` disassembly audit for any hot-path or generated-code claim.
3. **Closure budget in the same PR.** Every new `#import` inside a `<vstd/*.h>` header updates the layer table checked by Task 3.15's cohesion gate. A header that grows a dependency silently is a failed review.
4. **Goose-style diagnostics.** Errors name the function, the root, the destination, and the line. Never a bare "cannot compile".
5. **Composition tests, not just ingredient tests** — the lesson of Task 5.5. Every container gets at least one test combining it with the plugin passes that police it (root safety, flatness, exhaustiveness).
6. **Collect benchmarks, claim nothing.** Wire each container into the future Task 4.1 harness shape now (cycle counts recorded per reference machine as artifacts); publish no numbers.
7. **Every task ships a runnable example.** Each task adds at least one example under `examples/` demonstrating the features/capabilities it lands, and at least one *illegal* example under `examples/banned/` showing the construct the plugin must reject. Update `examples/README.md` in the same PR with a build line for each new example (see the example contract below). An example that doesn't build is a failed review; a banned example that *does* build is a broken gate.

### The example contract (applies to rule 7)

Every example entry in `examples/README.md` carries: name, one-line purpose, the task that introduced it, build command, and expected outcome (exit code / diagnostic text to grep for). Keep the layout flat:

```text
examples/
  README.md                 # table of all examples + build instructions
  vstring_view_intro.m      # legal usage, from Task 3.1
  banned/
    view_escapes_root.m     # must FAIL to compile; README quotes the diagnostic
```

Banned examples are compiled as part of the ban registry run (same machinery as `test_banned_*.m`): the build must fail, and the README records the expected diagnostic so a silent plugin regression is visible in CI.

---

## 🗺️ Dependency Map & Execution Waves

| Wave | Tasks | Why this order |
| :--- | :--- | :--- |
| **1 — Leaves** | 3.1 (string/span), 3.6 (strong), 3.7 (result), 3.2 (stack arrays) | Zero or rt-only dependencies; everything later wants them |
| **2 — Containers** | 3.3 (vector), 3.4 (map/hash), 3.5 (variant) | Build on wave 1; 3.5 pairs with the already-done plugin exhaustiveness pass |
| **3 — Concurrency & substrate** | 3.8 (channel/atomic), 3.9 (stream/ternary) | Plugin enforcement already exists (2.4.3, 2.6.x); this is the runtime half |
| **4 — Platform** | 3.10 (sys0), 3.11 (io), 3.12 (execution/vasync), 3.13 (simd) | Platform-specific, audit-heavy; parallelizable with waves 2–3 |
| **5 — Contracts & gate** | 3.14 (contracts), 3.15 (cohesion gate) | 3.14 feeds Task 4.4; 3.15 runs continuously from wave 1, formalized last |

### 📁 Header Layout & Layer Table (decision: per-component include roots)

Headers live in per-component include roots, extending the existing `rt/include/veriobjc_runtime.h` pattern:

```text
vstd/include/vstd/*.h      # <vstd/string.h>, <vstd/vector.h>, …
sys0/include/sys0/*.h      # freestanding floor (Task 3.10)
hw/include/hw/*.h          # device/real-time I/O (Task 3.11)
rt/include/veriobjc_runtime.h   # minimal micro-runtime floor (existing)
```

**Layer table** (a header may only `#import` from its own layer or below):

| Layer | May import |
| :--- | :--- |
| `rt` | (nothing — the floor) |
| `vstd` | `rt`, `vstd` |
| `sys0` | `rt`, `sys0` |
| `hw` | `rt`, `sys0`, `hw` |

**Enforcement is structural, not conventional.** Each component is a separate CMake interface target declaring only the include roots it may see, so an illegal cross-layer `#import` fails to compile outright. Task 3.15's gate double-checks the layer table against `clang -H` include-graph output and fails the build naming the header and the violated edge. Consumers pass one `-I` per component they use; a target that doesn't link `hw` physically cannot pull in a hardware header.

---

## 🌊 Wave 1 — Leaves

### Task 3.1: Text & Slices (`<vstd/string.h>`, `<vstd/span.h>`)

- **Goal:** non-owning `vstring_view` and contiguous `vspan<T>` — the zero-copy views everything else speaks in.
- **Red tests:** empty view; view over a DMA buffer (zero-copy assertion: no memcpy in disassembly); out-of-bounds access attempt; `span` of fixed array with compile-time extent deduction.
- **Invariant to hold:** a view is *rooted* — the plugin's reference-root safety (Task 2.4.2) must reject a view that outlives its target. **Composition test:** construct a view of a scope-local buffer, store it in an outer scope → expect the root-safety diagnostic, not a runtime crash.
- **Pitfall:** views of temporaries. The plugin should catch it; if it doesn't, that's a plugin bug to file, not a library workaround.

### Task 3.6: Strong Types (`<vstd/strong.h>`)

- **Goal:** `vstrong` newtype wrappers, zero implicit conversions.
- **Red tests:** no implicit conversion to/from the wrapped type; explicit unwrap only; `vstrong` never unwraps under `auto` inference (pairs with Task 2.5.7's deduction rules — test both directions).
- **Pitfall:** conversion operators sneaking back in via inherited constructors. Test the exact conversion paths you want banned.

### Task 3.7: Branch-Proven Result (`<vstd/result.h>`)

- **Goal:** `vresult<T, E>` with *required* branch checking before access — the runtime half of `return err from f` (lowering already done, Task 2.5.2).
- **Red tests:** accessing `.value` without a checked discriminant = compile error; checked access lowers to a jump, not a call; uncaught `from` at top level (plugin test exists — re-verify against the library type).
- **Pitfall:** silently allowing `.value` on a moved-from or error state. The linear ownership tracker (Task 2.4.1) should police this — composition test it.

### Task 3.2: Stack Arrays (`<vstd/stack.h>`)

- **Goal:** `vstack_array<T>` integrated with compiler-assigned data stacks — the header face of Phase 1's proven substrate.
- **Red tests:** growth never moves data (re-assert at header level with a million pushes + captured reference); one-store scope reset through the header API; **second resizable value while one is live = compile error** (the "one resizable on top per stack" rule — this is a toolchain-level check; test that the error names both values).
- **Pitfall:** the header accidentally routing through `malloc` for alignment shims. `nm` audit is the guard.

---

## 🌊 Wave 2 — Containers

### Task 3.3: Inplace Containers (`<vstd/vector.h>`)

- **Goal:** fixed-capacity `vvector<T, N>` with compile-time bounded assertions; built-in-place construction.
- **Red tests:** `push` beyond capacity = compile error naming the container and capacity; `push(parse(x))` writes directly into the slot (disassembly: no intermediate copy — pairs with Task 2.8.7's built-in-place verification); returning a growable array by value costs nothing.
- **Pitfall:** this is the **first real user of `vgeneric` at scale** — expect compiler friction, and treat every friction as signal: a container that can't be written cleanly in the language is a language bug, not a library workaround. File, don't finesse.

### Task 3.4: Cache-Aligned Maps (`<vstd/map.h>`, `<vstd/hash.h>`)

- **Goal:** `vflat_map` (parallel contiguous binary search — both branches predictable, SIMD-searchable) and `vhash_flat` (open addressing).
- **Red tests:** miss path; collision chain; load-factor ceiling = compile-time rejection at insertion; deterministic iteration order (content-addressing and report diffs depend on it).
- **Pitfall:** hidden allocation on rehash. Open addressing over a fixed `vvector` backing — `nm` audit.

### Task 3.5: Algebraic Data Types (`<vstd/variant.h>`)

- **Goal:** `vvariant` storage + `match` lowered to link-time jump tables. The plugin's exhaustiveness pass (Task 2.8.4) already exists — this is the data-plane half.
- **Red tests:** missing case = compile error; **adding a new variant case fails every non-exhaustive `match` downstream** (the acceptance criterion verbatim — make it a real test); `match` on a stack `vvariant` compiles to an indexed jump, no dispatch helper calls.
- **Pitfall:** variant storage size = max member + discriminant, exactly; `sizeof` audit like Task 1.3.1's zero-base-tax check.

---

## 🌊 Wave 3 — Concurrency & Substrate

### Task 3.8: Concurrency & Linear Guards (`<vstd/channel.h>`, `<vstd/atomic.h>`)

- **Goal:** lock-free SPSC flat-value channels (crossing = memcpy) and linear `vatomic<T>` (`.acquire()` returns a stack-bound wrapper; release inserted at scope exit).
- **Red tests:** non-flat send (plugin test exists, Task 2.4.3 — re-verify against the library type); SPSC invariant: two producers = compile error; `vatomic` payload accessed without acquire = compile error; scope-exit release visible as the hardware release instruction in disassembly.
- **Pitfall:** memory ordering. Default to the strongest ordering that measures no cost on the reference machine; document every relaxation with a benchmark artifact, not a hunch.

### Task 3.9: Streaming & Ternary Substrate (`<vstd/stream.h>`, `<vstd/ternary.h>`)

- **Goal:** packed ternary weights as `Bits(2)` codes (4/byte, consteval tables, BRAM-pinned), `from_bytes` verification on load (Task 1.4.2 exists — reuse it), and the `ll.ml/model/` blocks: token shift, GLU mux gates, shift-only RMSNorm, pre-banked KV arena with `ContextCap`.
- **Red tests:** context overflow = compile error (`test_banned_context_overflow.m` exists — extend to the arena API); weight image corrupt → `from_bytes` rejects before any kernel runs; ternary dot produces exactly `log2(N)` stages (elaboration test exists, Task 2.6.2 — keep in sync).
- **Pitfall (RESOLVED — latency contract):** the `cycles = 9` vs `cycles = 10` discrepancy was a category error, not an error: `ternary_dot<512>` is **9 cycles** (`clog2(512)`, matches Task 2.6.2's green `ReduceStages: 9` contract); `glu_gate` is combinational (**0 cycles**); the token shift is a register delay (**1 cycle**). So `TernaryGLUBlock` end-to-end is **10 cycles** (shift → parallel dots → mux). Example 3's cost table has the shift/glu line items swapped (totals coincidentally right) — fix the table and disambiguate plan.md Phase 4 wording ("9" = `ternary_dot` only) before Task 3.9. Going forward: the toolchain's elaboration contract (`vstream_latency_contract` / `--hw-report`) is authoritative; docs are regenerated from it, never hand-tuned.

---

## 🌊 Wave 4 — Platform

### Task 3.10: Freestanding Floor (`sys0/` / `ll.arch/`)

- **Goal:** arch-tuned `memcpy`/`memset`/`memcmp` (NEON/AVX-512, non-temporal stores), linker-script layouts, per-core boot trampolines, TLS via `GS`/`TPIDR_EL0`, MMU-as-library (page tables, huge pages, DMA-coherent regions, WB vs WC as distinct types), `rdtsc`/`CNTVCT_EL0` clocks.
- **Red tests:** byte-exact correctness vs a reference implementation across alignment cases (unaligned head/tail); non-temporal stores visible in disassembly; clock read is a single instruction, no libc.
- **Pitfall:** this is the most platform-specific task in the phase — audit-heavy, and the place where "works on my machine" dies. The loopback simulation transport (Task 3.11) is what keeps the rest of the phase testable without hardware.

### Task 3.11: Device & Real-Time I/O (`<vstd/io.h>`, `hw/`)

- **Goal:** non-blocking DMA line registration, direct ring-buffered streaming, driver-as-library, in-process loopback simulation transport.
- **Red tests:** loopback round-trip; ring overflow = compile-time bounded rejection (not runtime drop); blocking `read`/`write` on a constrained path = the ban-corpus diagnostic (row exists, `test_banned_blocking_io.m`).
- **Pitfall:** the simulation transport must implement the *identical* API — if the sim and the driver diverge, every downstream test lies.

### Task 3.12: Lazy Execution Pipelines (`<vstd/execution.h>` + `vasync`)

- **Goal:** P2300-style senders/receivers as an immutable caller-stack pipeline; `vasync` futures as compact stack-allocated state structs woken by hardware interrupt hooks.
- **Red tests:** cycle in the task DAG = compile error; WCET contract carried as a type-level value (assert it in a test); no polling ring in disassembly; wake-up only via declared interrupt hooks.
- **Pitfall:** depends on Task 2.8.6's topology machinery (done) — verify the integration rather than re-implementing.

### Task 3.13: SIMD (`<vstd/simd.h>`)

- **Goal:** compile-time vector width/ISA selection against the manifest-declared target; no raw intrinsics in user code on constrained paths.
- **Red tests:** target without the required ISA = clean configure-time error; intrinsic in user `@nolatency` code rejected; vectorized loop matches scalar reference byte-for-byte.
- **Pitfall:** silent fallback to scalar. A fallback must be a *named, reported* decision (`--hw-report`-style), never silent.

---

## 🌊 Wave 5 — Contracts & Gate

### Task 3.14: Contracts Library (`<vstd/contracts.h>`)

- **Goal:** `@pre`/`@post` surface with postcondition-to-SMT helper predicates — the vocabulary Task 4.4's proof integration consumes.
- **Red tests:** helper predicates are pure (effects report empty); contract extraction matches what the plugin emits for SMACK (golden-file test against Task 2.8's extractor output).
- **Pitfall:** designing predicates the SMT solver can't discharge. Prototype each helper's solvability *before* adding it to the public surface.

### Task 3.15: Cohesion & Minimal-Binary Gate

- **Goal:** formalize what this guide's standing rules have been enforcing informally: hello-world (zero vstd imports), `hello_uart.m` (io closure only), per-header closure budget (`vstd_cohesion_tests`), dead-strip assertion (`nm` on section symbols).
- **Red test:** deliberately add an umbrella `#import` to a leaf header → the gate must fail the build naming the header and the grown dependency.
- **Example-coverage check (pairs with standing rule 7):** the gate also parses `examples/README.md` and fails the build if any public `<vstd/*.h>` header has no example entry. Red test: add a public header without an example → the gate must fail naming the header.

---

## 🔧 Phase 3 Review Fix List (block Phase 4 until green)

Findings from the GitHub review of commits `36e98f2`–`9520184`. Work through in order; Phase 4's "no number, no claim" gate must not start on top of these.

- [x] **F1 — Task 3.3b: reimplement `vvector` over `vgeneric` (DONE; R1, R2, and Gate hole resolved).** `VVector<T : VVectorItem *>` shipped; frictions VG-001…VG-004 filed with reproducers before the plugin fixes; compound-literal temporary eliminated. **R1 (DONE — bridge accepted, option 3):** keep the C-macro container as an internal detail only — renamed `__vvector_storage_def`, header comment marks it deprecated, no new public dependents. **Exit criteria to one container:** successor **Task 3.3c** in `plan.md` implements plugin-synthesized `vvector<T, N>` sugar (pre-Sema lowering) once VG-005/VG-006 are lowered, then deletes `__vvector_storage_def`. **R2 (DONE):** filed **VG-005** (`vgeneric` lacks value/integer parameters → blocks `vvector<T, N>` syntax and parameterized capacity) and **VG-006** (flat `@interface` cannot be returned by value → blocks the return-by-value red test for `VVector<T>`) in `docs/veriobjc-vgeneric-frictions.md` with minimal reproducers. **Gate hole (DONE):** `tests/integration/check_vstd_cohesion.sh` now checks all headers with both `-x c-header` and `-x objective-c-header` (backed by a negative self-test for `#ifdef __OBJC__` cross-layer leaks in commit `bb1a477` → `8b8d4b9`), and `unit_rt_tests` compiles the `vstd` test suites as Objective-C++ (`-x objective-c++ -fno-exceptions`) with 100.00% `llvm-cov` coverage over the `#ifdef __OBJC__` methods of `VVector<T>`.
- [x] **F2 — plan.md Task 3.3b documentation (DONE; earlier "truncation" claim retracted).** The apparent repo plan.md truncation was a remote-fetch artifact, not a repo defect: local grep confirms Task 6.1–6.3 intact. Task 3.3b is documented in plan.md.
- [x] **F3 — Local audit trifecta on Waves 2–3 (DONE).** Recorded in the Phase 3 review audit artifact: mutation spot-check on root-safety analysis failed exactly the three root-escape tests under mutation (tests proven non-vacuous); `nm` audit shows zero libc/malloc/objc_msgSend across all Wave 2–3 objects; objdump audit covers by-value returns and both push paths on the post-3.3b implementation.
- [x] **F4 — Harden the cohesion gate's hello_uart check (DONE).** Deny-by-default via a non-IO `vstd/` import detector, with a built-in negative self-test using a synthetic unlisted header. Red/green commit pair per F5's protocol.
- [x] **F5 — Process going forward (ADOPTED — standing rule from Phase 4 on).** Proven during F1–F4: one Red/Green commit pair per fix, audits recorded as review artifacts. Standing from Phase 4 on: one commit per task, failing test visible before its implementation, and a short review-gate artifact at each wave boundary.

---

## 🎯 Where to Start (this week)

1. **Task 3.1, red test one:** `vstring_view` over a fixed buffer — construct, read, bounds-check, scope-exit. Then the composition test: view escaping its root scope must trip Task 2.4.2's diagnostic. That single red test exercises the newest plugin machinery against the oldest runtime invariant — exactly the kind of seam Phase 3 exists to stress.
2. Keep Task 3.15's cohesion script running from day one (it's ~30 lines of shell over `clang -H` or include-graph parsing) — retrofitting cohesion gates onto a grown library is a month of pain.
3. ~~Reconcile the `cycles = 9` vs `cycles = 10` latency discrepancy **before** Task 3.9~~ — resolved: `ternary_dot<512>` = 9 cycles, token shift = 1, `glu_gate` = 0, block = 10. Remaining work is doc-only: fix Example 3's swapped line items and plan.md Phase 4 attribution (see Task 3.9 pitfall).