# Installing VeriObjC

This document gets a fresh Linux machine from zero to a state where the VeriObjC repository builds and its CTest suites run. For CLI usage and build targets, see [`BUILDING.md`](BUILDING.md); this page only covers **installation**.

> [!NOTE]
> This is the **manual bootstrap**, maintained until `vpm` (Phase 6) ships. `vpm` will download, verify, and pin the toolchain itself — do not build convenience wrappers around this script; it is scaffolding to be replaced, not extended.

---

## 📋 Requirements

| Component | Version | Why this exact thing |
| :--- | :--- | :--- |
| **Clang / LLVM** | **23.x (pinned)** | The plugin is a C++ shared library linked against Clang/LLVM APIs. A version mismatch is an ABI error at load time, not a warning. The reference toolchain is Clang/LLVM 23.1.5. |
| **CMake** | ≥ 3.25 | Unified C23 / C++26 configuration; CTest runner. |
| **Make** | any | Build backend (via `build-essential`). Ninja also works (`-G Ninja`) but Make is the current reference path. |
| **CppUTest** | latest `master` | **Optional** — test framework for the `unit_rt`, `plugin`, and `integration` suites. Not needed to *use* VeriObjC, only to run its tests. Skip with `--no-cpputest`. The `4.0` tag is ~6 years stale; `master` is continuously maintained and is the recommended ref. |
| **Git** | any | Version control. |
| **OS** | Linux | The reference development path. Other Unixes: see *Manual steps* — untested, no claims. |
| **Z3** | **≥ 4.8.12 (64-bit)** | **Host/compile-time formal SMT solver** (`sudo apt-get install z3`) invoked by `--smt-verify` / `--smt-report` (Task 4.4) to discharge `@pre` / `@post` contracts over exact C bit-vectors (`QF_BV`). Z3 is strictly a compile-time host tool — compiled `veriObjC` binaries and `rt/` have **zero** runtime dependency on Z3 or `libz3`. Override path via `-DZ3_EXECUTABLE=/path/to/z3`, or bypass the CMake configure check when building without SMT verification via `-DVERIOBJC_REQUIRE_Z3=OFF`. |

---

## ⚡ Quick start (Debian / Ubuntu)

```bash
git clone <your-repo-url> veriObjC
cd veriObjC
bash scripts/install.sh          # prerequisites + Clang 23 + Z3 (>= 4.8.12) + CppUTest
#                                # (add --no-cpputest if you only want to use VeriObjC, not run its tests)
export CC=clang-23 CXX=clang++-23 CPPUTEST_ROOT="$HOME/.veriobjc/cpputest"
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
# Optional CMake overrides for Z3:
#   -DZ3_EXECUTABLE=/custom/path/z3    (pin a specific z3 >= 4.8.12 binary)
#   -DVERIOBJC_REQUIRE_Z3=OFF          (allow configuring without Z3 installed)
cmake --build build
ctest --test-dir build --output-on-failure
```

---

## 🔧 `scripts/install.sh`

```bash
#!/usr/bin/env bash
# VeriObjC toolchain bootstrap (manual path; `vpm` will replace this — Phase 6).
# Idempotent: safe to re-run. Nothing is installed into the repo itself.
set -euo pipefail

say() { printf '\033[1;34m[veriobjc-install]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[veriobjc-install]\033[0m %s\n' "$*" >&2; exit 1; }

LLVM_VERSION="${LLVM_VERSION:-23}"
CPPUTEST_REF="${CPPUTEST_REF:-master}"     # master deliberately: the 4.0 tag is ~6 years stale; the repo is actively maintained
DEPS_DIR="${VERIOBJC_DEPS:-$HOME/.veriobjc}"
SKIP_CPPUTEST=0
for arg in "$@"; do
  case "$arg" in
    --no-cpputest) SKIP_CPPUTEST=1 ;;
    *) die "Unknown option: $arg (supported: --no-cpputest)" ;;
  esac
done
SUDO=""
command -v sudo >/dev/null 2>&1 && SUDO="sudo"

[ "$(uname -s)" = "Linux" ] || die "This bootstrap targets Linux (the reference toolchain path)."
command -v apt-get >/dev/null 2>&1 || die "apt-get not found — see INSTALL.md 'Manual steps' for other distributions."

say "Installing build prerequisites (cmake, make, git, wget, z3)…"
$SUDO apt-get update -qq
$SUDO apt-get install -y build-essential cmake git wget z3

if ! command -v "clang-$LLVM_VERSION" >/dev/null 2>&1; then
  say "Installing Clang/LLVM $LLVM_VERSION from apt.llvm.org…"
  wget -qO /tmp/llvm.sh https://apt.llvm.org/llvm.sh
  $SUDO bash /tmp/llvm.sh "$LLVM_VERSION"
  $SUDO apt-get install -y \
    "clang-$LLVM_VERSION" \
    "llvm-$LLVM_VERSION" \
    "llvm-$LLVM_VERSION-dev" \
    "libclang-cpp-$LLVM_VERSION-dev"
fi

if [ "$SKIP_CPPUTEST" -eq 1 ]; then
  say "Skipping CppUTest (--no-cpputest) — unit_rt_tests won't build (-DBUILD_TESTING=OFF); that's fine if you only want to use VeriObjC."
else
  if [ ! -d "$DEPS_DIR/cpputest/.git" ]; then
    say "Fetching CppUTest ($CPPUTEST_REF) into $DEPS_DIR/cpputest…"
    git clone --depth 1 --branch "$CPPUTEST_REF" https://github.com/cpputest/cpputest.git "$DEPS_DIR/cpputest"
  fi
  if ! find "$DEPS_DIR/cpputest/cpputest_build" -name 'libCppUTest.a' 2>/dev/null | grep -q .; then
    say "Building CppUTest (static, Release)…"
    cmake -S "$DEPS_DIR/cpputest" -B "$DEPS_DIR/cpputest/cpputest_build" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$DEPS_DIR/cpputest/cpputest_build"
  fi
fi

say "Verifying toolchain…"
"clang-$LLVM_VERSION"  --version | head -n1
"clang++-$LLVM_VERSION" --version | head -n1
cmake --version | head -n1
command -v z3 >/dev/null 2>&1 || die "z3 not found on PATH after installation (requires Z3 >= 4.8.12)."
Z3_VER_LINE="$(z3 --version | head -n1)"
printf '%s\n' "$Z3_VER_LINE"
Z3_VER_NUM="$(printf '%s\n' "$Z3_VER_LINE" | grep -oE '[0-9]+\.[0-9]+\.[0-9]+' | head -n1 || true)"
[ -n "$Z3_VER_NUM" ] || die "Could not parse version from '$Z3_VER_LINE' (requires Z3 >= 4.8.12)."
if ! dpkg --compare-versions "$Z3_VER_NUM" ge "4.8.12"; then
  die "Installed Z3 version $Z3_VER_NUM is older than required minimum 4.8.12."
fi
if [ "$SKIP_CPPUTEST" -eq 0 ]; then
  CPPUTEST_LIB="$(find "$DEPS_DIR/cpputest/cpputest_build" -name 'libCppUTest.a' | head -n1)"
  [ -n "$CPPUTEST_LIB" ] || die "CppUTest static library not found — build failed silently, re-run with verbose output."
fi

if [ "$SKIP_CPPUTEST" -eq 1 ]; then
  cat <<EOF
Toolchain ready (no test framework). Environment for this session:

  export CC=clang-$LLVM_VERSION CXX=clang++-$LLVM_VERSION

Next steps (BUILDING.md has the full guide):
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
  cmake --build build

To run the test suites later: re-run scripts/install.sh without --no-cpputest.
EOF
else
  cat <<EOF
Toolchain ready. Environment for this session:

  export CC=clang-$LLVM_VERSION CXX=clang++-$LLVM_VERSION
  export CPPUTEST_ROOT="$DEPS_DIR/cpputest"

Next steps (BUILDING.md has the full guide):
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCPPUTEST_ROOT="$DEPS_DIR/cpputest"
  cmake --build build
  ctest --test-dir build --output-on-failure
EOF
fi
```

---

## ✅ Verifying the installation

A correct install satisfies all of these (lines marked *skip* don't apply to `--no-cpputest` installs):

```bash
clang-23 --version                                        # reports 23.x
z3 --version                                              # reports Z3 version >= 4.8.12 (64 bit)
test -f "${CPPUTEST_ROOT:-$HOME/.veriobjc/cpputest}/cpputest_build/src/CppUTest/libCppUTest.a"   # skip if --no-cpputest; or wherever your CMake expects it
cmake --build build && ctest --test-dir build --output-on-failure
```

The CTest gate names (from the plan) that must appear and pass **when CppUTest is installed**:

| CTest target | Suite |
| :--- | :--- |
| `unit_rt_tests` | Micro-runtime, `vstd`, `sys0`, and host Z3 bridge unit tests (CppUTest) |
| `plugin_tests` | Plugin AST/CFG/Z3 diagnostic-compliance tests |
| `examples_smoke_tests` | End-to-end examples + standalone `@sealed` binaries + Z3 contract proof smoke gate |
| `ban_corpus_tests` | Dual-direction ban corpus: clean under stock Clang, rejected under the plugin |
| `vstd_cohesion_tests` | Header layer table, `-ffreestanding -mno-sse` `rt/` audit, and `--gc-sections` minimal-binary gate |
| `e2e_sealed_polymorphism_tests` | Standalone `@sealed` base-pointer dispatch & mixed-size `vspan` scope-reset audit (zero `-lobjc`) |

---

## 🐧 Manual steps (other distributions)

The script covers Debian/Ubuntu via `apt.llvm.org`. Elsewhere, install the equivalents yourself and check the version pin:

- **Fedora**: `dnf install clang cmake ninja-build git z3` — verify `clang --version` reports 23.x and `z3 --version` reports ≥ 4.8.12; Fedora may ship a different LLVM major. If it is not 23, build the plugin against the distro's LLVM **only if** you accept ABI-mismatch risk, or install LLVM 23 from source.
- **Arch**: `pacman -S clang cmake ninja git z3` — Arch usually tracks current LLVM and Z3; verify Clang 23.x and Z3 ≥ 4.8.12.
- **macOS**: untested, no claims. The plugin builds as a `.dylib` on Apple platforms in principle; the freestanding runtime targets are not meaningful there.

The only hard rule: **the plugin must be built by and against the same Clang major version it loads into.** `clang-23` everywhere.

---

## 🩺 Troubleshooting

| Symptom | Cause | Fix |
| :--- | :--- | :--- |
| `libVeriObjCPlugin.so: undefined symbol` / binary mismatch on load | Plugin built against a different LLVM than the `clang` on `PATH` | `export CC=clang-23 CXX=clang++-23` and rebuild; verify `clang --version` |
| CMake can't find CppUTest | `CPPUTEST_ROOT` unset or wrong | Point `-DCPPUTEST_ROOT=...` or `export CPPUTEST_ROOT=...` at the CppUTest checkout containing `cpputest_build/` (or `build/`) |
| `unit_rt_tests` absent from CTest | Installed with `--no-cpputest`, or tests disabled at configure time | Re-run `scripts/install.sh` without the flag; rebuild with tests enabled |
| `llvm-23-dev` missing | Script ran on a machine where `llvm.sh` partially failed | `sudo apt-get install llvm-23-dev libclang-cpp-23-dev` |
| Plugin tests skipped silently | Plugin not registered under `-plugin veriobjc-check` | Check the CMake option enabling plugin tests; see BUILDING.md |
| `error: unknown attribute` on `objc_sealed` etc. | Compiling without the plugin | Run `clang-23` with `-Xclang -load -Xclang libVeriObjCPlugin.so -add-plugin veriobjc-check` (BUILDING.md documents the wrapper) |
| CMake fails with `Z3 SMT solver not found` or `requires Z3 >= 4.8.12` | `z3` missing on `PATH` or older than `4.8.12` | Install via `sudo apt-get install z3`, point `-DZ3_EXECUTABLE=/path/to/z3` at a ≥ 4.8.12 binary, or pass `-DVERIOBJC_REQUIRE_Z3=OFF` when configuring without SMT contract checks |

---

## 🔮 When `vpm` lands (Phase 6)

Task 6.1 gives this job to a single static Go binary that downloads, verifies, and pins the exact toolchain — at which point this page shrinks to:

```bash
curl -O https://…/vpm && ./vpm sync
```

Until then, `scripts/install.sh` is authoritative. Do not add distro detection beyond what is here; that is `vpm`'s job.
