# Installing VeriObjC

This document gets a fresh Linux machine from zero to a state where the VeriObjC repository builds and its CTest suites run. For CLI usage and build targets, see [`BUILDING.md`](BUILDING.md); this page only covers **installation**.

> [!NOTE]
> This is the **manual bootstrap**, maintained until `vpm` (Phase 6) ships. `vpm` will download, verify, and pin the toolchain itself — do not build convenience wrappers around this script; it is scaffolding to be replaced, not extended.

---

## 📋 Requirements

| Component | Version | Why this exact thing |
| :--- | :--- | :--- |
| **Clang / LLVM** | **23.x (pinned)** | The plugin is a C++ shared library linked against Clang/LLVM APIs. A version mismatch is an ABI error at load time, not a warning. The reference toolchain is Clang/LLVM 23.1.5. |
| **CMake** | ≥ 3.24 | Unified C23 / C++26 configuration; CTest runner. |
| **Make** | any | Build backend (via `build-essential`). Ninja also works (`-G Ninja`) but Make is the current reference path. |
| **CppUTest** | latest `master` | **Optional** — test framework for the `unit_rt`, `plugin`, and `integration` suites. Not needed to *use* VeriObjC, only to run its tests. Skip with `--no-cpputest`. The `4.0` tag is ~6 years stale; `master` is continuously maintained and is the recommended ref. |
| **Git** | any | Version control. |
| **OS** | Linux | The reference development path. Other Unixes: see *Manual steps* — untested, no claims. |
| SMACK / Z3 | *not yet* | Phase 4 (Task 4.4) dependency. Do not install now; this page will grow a section when that task lands. |

---

## ⚡ Quick start (Debian / Ubuntu)

```bash
git clone <your-repo-url> veriObjC
cd veriObjC
bash scripts/install.sh          # prerequisites + Clang 23 + CppUTest
#                                # (add --no-cpputest if you only want to use VeriObjC, not run its tests)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
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

say() { printf '\033[1;34m[veriobjc-install]\033[0m %s\n' "$*"; }
die() { printf '\033[1;31m[veriobjc-install]\033[0m %s\n' "$*" >&2; exit 1; }

[ "$(uname -s)" = "Linux" ] || die "This bootstrap targets Linux (the reference toolchain path)."
command -v apt-get >/dev/null 2>&1 || die "apt-get not found — see INSTALL.md 'Manual steps' for other distributions."

say "Installing build prerequisites (cmake, ninja, git, wget)…"
$SUDO apt-get update -qq
$SUDO apt-get install -y build-essential cmake git wget

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
  say "Skipping CppUTest (--no-cpputest) — the test suites won't build; that's fine if you only want to use VeriObjC."
else
  if [ ! -d "$DEPS_DIR/cpputest/.git" ]; then
    say "Fetching CppUTest ($CPPUTEST_REF) into $DEPS_DIR/cpputest…"
    git clone --depth 1 https://github.com/cpputest/cpputest.git "$DEPS_DIR/cpputest"
  fi
  if ! find "$DEPS_DIR/cpputest/build" -name 'libCppUTest.a' 2>/dev/null | grep -q .; then
    say "Building CppUTest (static, Release)…"
    cmake -S "$DEPS_DIR/cpputest" -B "$DEPS_DIR/cpputest/build" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$DEPS_DIR/cpputest/build"
  fi
fi

say "Verifying toolchain…"
"clang-$LLVM_VERSION"  --version | head -n1
"clang++-$LLVM_VERSION" --version | head -n1
cmake --version | head -n1
if [ "$SKIP_CPPUTEST" -eq 0 ]; then
  CPPUTEST_LIB="$(find "$DEPS_DIR/cpputest/build" -name 'libCppUTest.a' | head -n1)"
  [ -n "$CPPUTEST_LIB" ] || die "CppUTest static library not found — build failed silently, re-run with verbose output."
fi

if [ "$SKIP_CPPUTEST" -eq 1 ]; then
  cat <<EOF
Toolchain ready (no test framework). Environment for this session:

  export CC=clang-$LLVM_VERSION CXX=clang++-$LLVM_VERSION

Next steps (BUILDING.md has the full guide):
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build

To run the test suites later: re-run scripts/install.sh without --no-cpputest.
EOF
else
  cat <<EOF
Toolchain ready. Environment for this session:

  export CC=clang-$LLVM_VERSION CXX=clang++-$LLVM_VERSION
  export CPPUTEST_DIR="$DEPS_DIR/cpputest" CPPUTEST_HOME="$DEPS_DIR/cpputest"

Next steps (BUILDING.md has the full guide):
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
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
test -f "${CPPUTEST_DIR:-$HOME/.veriobjc/cpputest}/build/libCppUTest.a"   # skip if --no-cpputest; or wherever your CMake expects it
cmake --build build && ctest --test-dir build --output-on-failure
```

The CTest gate names (from the plan) that must appear and pass **when CppUTest is installed**:

| CTest target | Suite |
| :--- | :--- |
| `unit_rt_tests` | Micro-runtime unit tests (CppUTest) |
| `plugin_tests` | Plugin diagnostic-compliance tests |
| `ban_corpus_tests` | Dual-direction ban corpus: clean under stock Clang, rejected under the plugin |
| `examples_smoke_tests` | End-to-end examples: 0 `objc_msgSend` / 0 undefined symbols |

---

## 🐧 Manual steps (other distributions)

The script covers Debian/Ubuntu via `apt.llvm.org`. Elsewhere, install the equivalents yourself and check the version pin:

- **Fedora**: `dnf install clang cmake ninja-build git` — verify `clang --version` reports 23.x; Fedora may ship a different LLVM major. If it is not 23, build the plugin against the distro's LLVM **only if** you accept ABI-mismatch risk, or install LLVM 23 from source.
- **Arch**: `pacman -S clang cmake ninja git` — Arch usually tracks current LLVM; verify 23.x.
- **macOS**: untested, no claims. The plugin builds as a `.dylib` on Apple platforms in principle; the freestanding runtime targets are not meaningful there.

The only hard rule: **the plugin must be built by and against the same Clang major version it loads into.** `clang-23` everywhere.

---

## 🩺 Troubleshooting

| Symptom | Cause | Fix |
| :--- | :--- | :--- |
| `libVeriObjCPlugin.so: undefined symbol` / binary mismatch on load | Plugin built against a different LLVM than the `clang` on `PATH` | `export CC=clang-23 CXX=clang++-23` and rebuild; verify `clang --version` |
| CMake can't find CppUTest | `CPPUTEST_DIR` / `CPPUTEST_HOME` unset or wrong | Point either env var at the CppUTest checkout containing the built static lib |
| `unit_rt_tests` absent from CTest | Installed with `--no-cpputest`, or tests disabled at configure time | Re-run `scripts/install.sh` without the flag; rebuild with tests enabled |
| `llvm-23-dev` missing | Script ran on a machine where `llvm.sh` partially failed | `sudo apt-get install llvm-23-dev libclang-cpp-23-dev` |
| Plugin tests skipped silently | Plugin not registered under `-plugin veriobjc-check` | Check the CMake option enabling plugin tests; see BUILDING.md |
| `error: unknown attribute` on `objc_sealed` etc. | Compiling without the plugin | Run `clang-23` with `-Xclang -load -Xclang libVeriObjCPlugin.so -add-plugin veriobjc-check` (BUILDING.md documents the wrapper) |

---

## 🔮 When `vpm` lands (Phase 6)

Task 6.1 gives this job to a single static Go binary that downloads, verifies, and pins the exact toolchain — at which point this page shrinks to:

```bash
curl -O https://…/vpm && ./vpm sync
```

Until then, `scripts/install.sh` is authoritative. Do not add distro detection beyond what is here; that is `vpm`'s job.
