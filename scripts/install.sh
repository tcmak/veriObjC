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
  cmake --install build --prefix "\$HOME/.local"   # installs bin/veriobjc-cc, lib/veriobjc/, include/veriobjc/

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
  cmake --install build --prefix "\$HOME/.local"   # optional: install self-contained veriobjc-cc SDK
EOF
fi
