# 🧭 Phase 7 Kickoff — 7a Execution Guide, Trigger Map & Evidence Templates

Three parts. **Part A** is the execution guide for Block 7a — the one execution-ready block of the draft plan (library headers over already-shipped `rt/`/`vstd/`/`sys0/` primitives, zero compiler changes). **Part B** is the trigger/decision map for the whole Phase 7–8 span: which decisions are open, which artifact resolves them, and when each deferred guide gets written. **Part C** holds the two evidence templates (friction log, memo) so their format is pinned before the first entry exists.

Advisory — your TDD judgment governs; this guide is the map, not the law. Structure and standing rules inherit from the Phase 3 execution guide.

---

## Part A — Phase 7a Execution Guide (Low-Latency Library Kit)

### 📏 Standing rules (inherited from Phase 3, plus 7a additions)

Inherited, unchanged: red first, always; the audit trifecta; closure budget in the same PR (cohesion gate); Goose-style diagnostics; composition tests, not just ingredient tests; collect benchmarks, claim nothing; every task ships a runnable example + one banned example (the example contract applies as written in the Phase 3 guide).

Additions for 7a:

8. **Zero-compiler-change discipline.** If a task discovers it *needs* a new plugin check, the task stops there: record the need as a friction-log entry (LG-xxx, Part C) and finish the task with the workaround explicitly documented. Silently adding a check inside 7a breaks the two-tier design (7c exists to promote proven needs).
9. **Capability hygiene (7a.2).** Capabilities are produced *only* by `@cold @io` boot functions, are opaque (direct field access rejected by the existing `vatomic`-pattern ban), and every capability provider ships a freestanding fallback so CI stays hosted-toolchain-free and deterministic.
10. **Virtual clock rule.** No wall-clock reads in any 7a test. Time flows through the injected tick source; `sys0_rdtsc` appears only behind calibration (7a.1) or in host bench harnesses.
11. **`sys0/` stays hardware-shaped.** No POSIX semantics, no malloc-like APIs, no sockets. The seam vocabulary is: rings, reservations, calibration.

### 🗺️ Wave map

| Wave | Tasks | Why this order |
| :--- | :--- | :--- |
| **1 — Time floor** | 7a.1 (calibration, `vticks`/`vns`, virtual tick source) | Everything later measures or schedules against it |
| **2 — Capabilities** | 7a.2 (opaque capability types + `sys0` reservations + freestanding fallback) | Parallelizable with wave 1; feeds the runloop and the barrier story |
| **3 — Timers** | 7a.3 (timing wheel, deadlines) | Depends on 7a.1 tick types |
| **4 — Runloop** | 7a.4 (pollables, deterministic `step()`) | Depends on 7a.3 + shipped Task 3.8/3.11 primitives |
| **5 — Journal & skeleton** | 7a.5 (journal, replay harness, fixture corpus), 7a.6 (`vpm` template, rides Task 6.1) | Depends on 7a.2 + 7a.4; 7a.5 is the 7b pipeline's evidence engine |

### Per-task walkthrough

**Task 7a.1 — Boot calibration & typed clock domains.**
Red: a cross-domain mix test — `vticks + vns` must fail to compile with the strong-type diagnostic; a calibration test asserting the pinned multiplier/offset reproduces a known tick→ns mapping on the virtual source. Green: `@cold` calibration function writing a read-only calibration record; `vticks`/`vns` as `vstd/strong.h` types. Acceptance: drift/resolution contract documented per platform; all tests green on the virtual clock.

**Task 7a.2 — Opaque boot capability types.**
Red: forged capability construction and direct field access must fail (reuse the `vatomic` direct-access ban; extend the annotate allowlist only if the opaque pattern needs its own spelling — if so, that's friction entry LG-001 by rule 8). Green: `vcore_set`/`varena_cap`/`vmap_cap` + `@cold @io` reservation functions in `<sys0/mem.h>`/`<sys0/mmu.h>`. Acceptance: freestanding fallback (`veriobjc_stack` backing) green in CI; ban rows `test_banned_capability_direct_access.m`, `test_banned_capability_forged_init.m`.

**Task 7a.3 — Timing wheel.**
Red: wheel overflow/underflow fails closed with a named diagnostic; deadline round-trip exact under the virtual tick source. Green: constant-bounds hierarchical wheel over `vticks`. Acceptance: bounds are compile-time constants; deterministic across runs.

**Task 7a.4 — Runloop & pollables.**
Red: a `@blocking`/`@io`/`@alloc` call inside the composed `step()` path fails via the *shipped* Task 2.8.1 checks (no new plugin work); an unbounded `step()` is rejected by documented bounds. Green: pollable composition (rings, channels, timers) with per-step bound constants documented in the header contract. Acceptance: deterministic step traces on the virtual clock; example wired into the smoke suite.

**Task 7a.5 — Journal & replay.**
Red: journal divergence produces the named fail-closed diagnostic; a corrupted journal record is rejected by the checksum chain. Green: content-addressed journal (VOBJ v1-style framing, FNV-1a) + host replay harness; `fixtures/journal_v1/` corpus started with accept/reject goldens. Acceptance: run → journal → replay bit-exact; overhead figure self-declared in the header (measured artifact collected, not claimed).

**Task 7a.6 — `vpm` template (rides Task 6.1).**
Acceptance: `vpm init --template=lowlatency` produces a *building* example composing 7a.1–7a.5; the example contract applies (README table row + build line).

### ⚠️ Traps

- Adding plugin checks inside 7a (see rule 8 — the friction log is the mechanism, not a shortcut).
- `sys0/` growing POSIX semantics; the day `<sys0/time.h>` wants `clock_gettime`, file a friction entry instead.
- Measuring on wall time (rule 10); the bench harness is the only exception, and it records artifacts, not claims.
- Capability leakage: a `@cold`-reserved region consumed raw by steady code instead of through its capability type is a review failure even when it compiles.

---

## Part B — Trigger / Decision Map (Phases 7–8)

| # | Decision point | Resolved by (artifact) | Gates | Full guide written |
| :--- | :--- | :--- | :--- | :--- |
| 1 | Does the kit need plugin help? (7e.6, and any 7a workarounds) | **7b.2 friction log** (LG-xxx entries, template in Part C) | Resolves 7e.6 build/no-build; evidence input to 7c | 7b guide at 7a completion |
| 2 | Which language properties get promoted? | **7b.4 memo** (verdicts from 7b.3 artifacts + 7e.5 evidence) | Gates 7c.1–7c.4 (fail-closed: no memo row, no planning) | 7c guide written *from* the memo |
| 3 | Is the diagnostic vocabulary governed? | **7d.1 descriptor registry + 7d.3 compliance gate** | Gates *every* Phase 8 diagnostic (8.1.1, 8.3.1 must be born registered) | Phase 8 guide written from the registry + 8.0 inventory |
| 4 | Do special-purpose bounds checks survive? | **Task 8.0 reconciliation inventory** | Decides demotion of 7c.3 `lmax`, 7a.4 contracts, 7e.1 index obligations onto refinements | Folded into the Phase 8 guide |
| 5 | Does zero-erasure hold on a real pipeline? | **7b.5 v2 re-baseline** (post-8.1/8.2/8.3 artifact diff) | Closes Phase 8's acceptance; extends 8.3.2's audit to a measured artifact | (no separate guide; runbook is Task 7b.5) |

Sequencing summary: 7a guide = now (Part A). 7b guide = at 7a completion. 7c guide = from the 7b.4 memo. Phase 8 guide = from the 7d.1 registry + 8.0 inventory. Nothing in Phase 7c or 8 is planned from imagination; each is planned from the artifact that resolves its uncertainty.

---

## Part C — Evidence Templates

### Friction log entry (7b.2; `docs/veriobjc-lowlatency-frictions.md`)

```text
### LG-00N: <short title>
- Found while:   <task / example / file:line>
- Symptom:       <what the kit could not express, or what had to be worked around>
- Workaround:    <exactly what was done instead; no silent workarounds>
- Candidate:     <7c.1 | 7c.2 | 7c.3 | 7c.4 | 7e.6 | none>
- Recommendation: <promote | defer | reject> — <one-line reason>
- Status:        <open | resolved-by <task>> 
```

Rules: entries are append-only; verdicts use the fixed vocabulary; a workaround that never got an entry is a review failure (same discipline as the `vgeneric` frictions document, VG-001…VG-006).

### 7b.4 memo row (Block 7c justification)

```text
| Candidate | Evidence (artifact paths) | Friction rows | Verdict | Revisit condition |
| :--- | :--- | :--- | :--- | :--- |
| 7c.1 effect-carrying function types | <bench/replay/audit artifacts> | <LG-xxx, …> | <promote | defer | reject> | <what new evidence would change it> |
```

Rules: a candidate without evidence rows is `defer` by default (fail-closed); `promote` requires artifacts that exist and are committed, not projections; the memo cites example numbers only as recorded artifacts ("no number, no claim").