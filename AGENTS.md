# AGENTS.md — `veriObjC` Working Protocol & Standing Engineering Rules

`veriObjC` is a deterministic, zero-runtime-overhead Objective-C subset compiler plugin (`compiler/`), freestanding runtime (`rt/`), and verifiable standard library (`vstd/`).

## 1. Standalone Repository Scope & Boundaries

* **`veriObjC` OWNS:** Language subset bans & static lowering (`compiler/`), strictly freestanding runtime (`rt/`), `vobj_v1` `RelPtr` container (`fixtures/vobj_v1/`), `vstd/` containers and pure-CPU round-half-up software fallback kernels (`vstd/include/vstd/ternary.h`), `sys0/`/`hw/` freestanding substrates, and `host/` benchmark/audit headers. `veriObjC` is completely standalone with **zero** external repository dependencies or git submodules.
* **`veriObjC` DOES NOT OWN:** External model weight container specifications or wire parsers, FPGA hardware RTL or register maps, RTL LUT estimators, or hardware co-simulation oracles.

---

## 2. Session Workflow & Version Control

1. **Orient first:** Read `README.md` and `plan.md` at the start of every session. Work on **one task at a time**, in `plan.md` order, unless told otherwise.
2. **Stop on open decisions:** If a task hits an unresolved language, ABI, or lowering trade-off, **stop and ask**—never paper over an ambiguity with a silent stub, text-rewrite hack, or mock stand-in.
3. **Atomic commits:** Commit after each satisfied acceptance criterion using the format `<task-id>: <what>`. Enforce LF line endings (`* text=auto eol=lf`).
4. **Close the loop:** End every working session by updating task checkboxes in `plan.md` and keeping `README.md` status claims strictly aligned with passing tests (untick or label placeholders honestly).

---

## 3. Standing Engineering Rules (Apply to Every Task)

1. **Red first, never weaken tests.**
   * No plugin ban, lowering pass, runtime primitive, or `vstd` header lands without its failing unit, plugin, or integration test written and observed red first.
   * **Never weaken a test, coverage threshold, ban corpus case, or golden fixture to get green.** If a test expectation looks wrong, stop and flag the discrepancy to the maintainer.

2. **The Audit Trifecta** for every runtime (`rt/`) and lowered codegen piece:
   * `llvm-cov` 100% region/function/branch coverage.
   * `nm` zero libc, `malloc`, `float`/`double`, `objc_msgSend`, or `__objc_exec_class` symbols in freestanding targets (`rt/` must contain zero host benchmarking, report-diff, or scheduler headers).
   * `llvm-objdump` disassembly audit for any hot-path or static-dispatch claim.

3. **Closure budget, pinned build discovery & zero text-rewriting hacks.**
   * Every new `#import` / `#include` inside `<vstd/*.h>` updates the layer table checked by `tests/integration/check_vstd_cohesion.sh` in the same commit.
   * CMake must discover LLVM and CppUTest cleanly via standard variables (`LLVM_DIR`, `CPPUTEST_ROOT`)—never hard-code machine-specific `/usr/lib/llvm-*` paths.
   * All language bans (scope escape, use-after-move, zero-DSP transitive calls) and syntax extensions must operate on Clang's AST / `clang::CFG` dataflow—never via fragile source-text find-and-replace or single-shape AST pattern hacks.

4. **Goose-style diagnostics.**
   Plugin errors name the function, the root/provenance, the destination, and the source line/column. Never emit a bare "cannot compile".

5. **Composition & escape-corpus tests, not just happy-path shapes.**
   * Every container gets at least one test combining it with the plugin passes that police it (root safety, flatness, exhaustiveness, polymorphic dispatch through base pointers).
   * Every plugin ban ships with an adversarial escape corpus (aliases, struct fields, globals, out-params, `&inner[0]`, helper functions, loop back-edges) alongside branch-symmetric positive tests.

6. **Collect benchmarks, claim nothing (Zero Certification Theater).**
   Record cycle counts in the host bench harness as raw artifacts; publish no unverified claims. Never emit `UNSAT_VERIFIED` without invoking a real SMT solver, never print constant LUT numbers as hardware reports, and never certify external repositories with local stand-ins.

7. **Every task ships runnable positive + banned examples.**
   Each feature task adds at least one runnable example under `examples/` and at least one *illegal* example under `examples/banned/` that the plugin must reject, updating `examples/README.md` in the same commit. An example that fails to build—or a banned example that *succeeds*—is a broken gate.

---

## 4. Mandatory Pre-Completion Gate

A task is done **only** when every check below passes cleanly:
1. Clean `cmake` configure + build with zero warnings.
2. `ctest --output-on-failure`
3. All scripts in `tests/integration/` (`check_vstd_cohesion.sh`, `run_ban_corpus_test.sh`, `run_examples_smoke_test.sh`) pass.
