# Contributing to VeriObjC

VeriObjC is a research PoC with unusual constraints: a closed world, a ban registry, and a "no number, no claim" discipline. Contributions are welcome, but this document is strict about process — in this project, the process *is* the product.

---

## 🧭 Engineering Principles (Read First)

1. **No number, no claim.** No performance figure, cycle count, or coverage number appears in a PR without a benchmark or CI artifact behind it. "Faster" is not a review argument; a diffed report is.
2. **A ban that isn't tested isn't a ban.** Every rejected construct has a registry row, a named diagnostic, and a test asserting that exact diagnostic fires. Never weaken a diagnostic to make a test pass.
3. **A negative test that isn't valid Objective-C proves nothing.** Every rejection-corpus file must compile `-Werror` clean under stock Clang 23 *and* fail under the plugin with the named diagnostic.
4. **The closed world is the product.** Proposals that reintroduce dynamism (runtime loading, reflection, open hierarchies) are not features here — they are category errors. If you need them, this is the wrong project, by design.

---

## 🔴 The TDD Gate

All implementation work follows **Red ➔ Green ➔ Refactor ➔ Audit**. No task merges without its red test first.

- **Red:** Write the failing test first — positive tests (feature works) and negative tests (`tests/` expected-failure files like `test_banned_*.m`). A PR that adds behavior without its red test is returned without review.
- **Green:** Minimum implementation to pass. No drive-by features.
- **Refactor:** Keep diagnostics Goose-style: name the root, the destination, the function, and the line — never a bare "error: cannot compile".
- **Audit:** For runtime work, this is mandatory and recorded in the PR: coverage (`llvm-cov`, 100% region/function/branch is the bar set by Phase 1) and, where the claim is about generated code, disassembly (`llvm-objdump`) — e.g. "one-store scope free" claims must show the single `mov` store.
- **Examples:** A PR that change behavior without examples is returned without review. Please refer to code examples under the examples. our toolchain needs to be able to build them. 

---

## 🚫 What We Do Not Accept

- **Features from the ban list.** Check [`docs/veriobjc-ban-list.md`](docs/) before proposing anything; if the construct is banned, the answer is the registry's replacement, not an exception.
- **New language features without the full chain.** A feature lands only with all four: specification section, ban/registry rows for what it rejects, positive tests, and negative tests with named diagnostics.
- **Silent broadening.** Anything that lets an `id`, an unannotated `extern fn` call from `@nolatency`, a non-flat channel payload, or a data-stack reference escape past the plugin is a correctness bug regardless of what tests pass.
- **Unbenchmarked performance prose.** Doc changes claiming speed are rejected on sight unless they cite a benchmark ID reproducible from CI artifacts.

---

## 🔁 Ban Registry Cross-Reference Rules

The registry (`docs/veriobjc-ban-list.md`) and the test suite are kept in lockstep in every PR:

- A registry row without a green test is an **open defect** — the PR adding the row must add the test, or mark it ⬜ with a tracking issue.
- A test without a registry row is an **undocumented ban** — either add the row or delete the test.
- Changing a diagnostic string is a breaking change: update the registry row and every diagnostic-compliance test in the same PR. CI asserts diagnostic *text*, not just compilation failure.

---

## 📊 Report Discipline

`--effects-report`, `--branch-report`, `--mono-report`, and `--hw-report` are CI artifacts, diffed per build:

- A certified hot loop that *gains* an effect, an unproven branch, or an instantiation fails CI even when all tests pass. Do not "fix" this by editing the report expectations — fix the code or renegotiate the budget in the manifest, in the PR, out loud.
- Latency budgets are versioned artifacts reviewed like code. A p99 regression beyond the measured noise band fails the build, naming the commit.

---

## 🛠️ Toolchain & Build

- **Toolchain:** Clang / LLVM 23.1.5 (the plugin pins the exact version; mismatches are a setup error, not a compatibility bug to code around).
- **Build:** CMake + Make/Ninja; tests via CppUTest and CTest.
- **Test suites:** `unit_rt` (micro-runtime), `plugin` (diagnostic-compliance), `integration` (end-to-end pipeline, ban corpus).
- See [`BUILDING.md`](BUILDING.md) for setup, CLI usage, and running the full gate locally. A PR is not reviewable until the full gate passes on your machine.

---

## ✅ PR Checklist

- [ ] Red test written first, and committed before the implementation (reviewable history)
- [ ] Positive **and** negative tests; negative tests fail with the exact named diagnostic
- [ ] An new / change in code example under the examples directory
- [ ] New bans: registry row + rejection-corpus file + diagnostic-compliance test
- [ ] Coverage audit attached for runtime changes (100% region/function/branch)
- [ ] Disassembly audit attached for any generated-code claim
- [ ] Report diffs clean (`--effects-report`, `--branch-report`, `--mono-report`, `--hw-report`)
- [ ] No performance claims without a benchmark ID from CI artifacts
- [ ] Docs updated in the same PR (spec section, registry, mapped notes in the TDD plan)

## 🐛 Reporting Issues

- **Compiler accepted something it shouldn't:** file it with a minimal `.m` reproducer. It must compile under stock Clang 23 — otherwise it isn't a VeriObjC bug.
- **A diagnostic is unclear:** paste the exact output. Diagnostics are the UX of this project; unclear ones are treated as bugs.
- **A proof or report looks wrong:** include the report artifact and the invocation that produced it.

---

*VeriObjC is deliberately small and restrictive. The bar for merging is that the language gets harder to violate, not bigger to use.*
