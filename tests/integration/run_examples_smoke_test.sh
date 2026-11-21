#!/bin/bash
set -e

PLUGIN_PATH="$1"
REPO_ROOT="$2"

if [ -z "$REPO_ROOT" ]; then
    SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
    REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
fi

if [ -z "$PLUGIN_PATH" ]; then
    PLUGIN_PATH="$REPO_ROOT/build/libVeriObjCPlugin.so"
fi

if [ ! -f "$PLUGIN_PATH" ]; then
    echo "ERROR: Plugin not found at $PLUGIN_PATH"
    exit 1
fi

echo "=== VeriObjC Examples Smoke & Acceptance Test Suite ==="
echo "Plugin: $PLUGIN_PATH"
echo "Repository Root: $REPO_ROOT"

TMP_OBJ=$(mktemp /tmp/veriobjc_example_XXXXXX.o)
TMP_SEALED_BIN=$(mktemp /tmp/veriobjc_sealed_synth_XXXXXX)
trap 'rm -f "$TMP_OBJ" "$TMP_SEALED_BIN"' EXIT

# 1. Compile and audit Objective-C (.m) examples
for m_file in "$REPO_ROOT"/examples/*.m; do
    base_name=$(basename "$m_file")
    echo "[RUN] Objective-C Example: $base_name"

    clang-23 -std=gnu23 -O2 -Werror -fblocks \
             -Xclang -load -Xclang "$PLUGIN_PATH" \
             -Xclang -add-plugin -Xclang veriobjc-check \
             -I"$REPO_ROOT/rt/include" \
             -I"$REPO_ROOT/vstd/include" \
             -I"$REPO_ROOT/sys0/include" \
             -I"$REPO_ROOT/hw/include" \
             -c "$m_file" \
             -o "$TMP_OBJ"

    # Verify zero dynamic dispatch, ObjC runtime constructor, or block runtime references
    if nm "$TMP_OBJ" | grep -qE "objc_msgSend|objc_msg_lookup|__objc_exec_class|_OBJC_CLASS_|_OBJC_METACLASS_|objc_enumerationMutation|_NSConcreteStackBlock|_NSConcreteGlobalBlock|_Block_object_assign"; then
        echo "[FAIL] $base_name emitted forbidden dynamic dispatch, ObjC runtime, or block runtime symbol!"
        nm "$TMP_OBJ"
        exit 1
    fi

    # Verify zero heap allocator references (malloc / free)
    if nm "$TMP_OBJ" | grep -qE "U malloc|U free"; then
        echo "[FAIL] $base_name emitted forbidden heap allocator reference!"
        nm "$TMP_OBJ"
        exit 1
    fi

    echo "[PASS] $base_name compiled cleanly (0 warnings, 0 dynamic dispatch calls, 0 heap calls)."
done

# 2. Compile and audit Freestanding C (.c) kernel examples
for c_file in "$REPO_ROOT"/examples/*.c; do
    base_name=$(basename "$c_file")
    echo "[RUN] Freestanding Kernel Example: $base_name"

    clang-23 -O2 -ffreestanding -nostdlib -Werror \
             -Xclang -load -Xclang "$PLUGIN_PATH" \
             -Xclang -add-plugin -Xclang veriobjc-check \
             -I"$REPO_ROOT/rt/include" \
             -I"$REPO_ROOT/sys0/include" \
             -c "$c_file" \
             -o "$TMP_OBJ"

    # Verify zero undefined external symbols (U)
    if nm "$TMP_OBJ" | grep -q " U "; then
        echo "[FAIL] $base_name contains undefined external symbol(s) in freestanding mode!"
        nm "$TMP_OBJ"
        exit 1
    fi

    echo "[PASS] $base_name compiled cleanly in freestanding mode (0 undefined external symbols)."
done

# 3. Link & run standalone @sealed executables end-to-end WITHOUT -lobjc
for standalone_ex in "sealed_synth_hierarchy.m" "sealed_mixed_scope.m"; do
    echo "[RUN] Standalone Executable Example: $standalone_ex"
    clang-23 -std=gnu23 -O2 -Werror -fblocks \
             -Xclang -load -Xclang "$PLUGIN_PATH" \
             -Xclang -add-plugin -Xclang veriobjc-check \
             -fpass-plugin="$PLUGIN_PATH" \
             -I"$REPO_ROOT/rt/include" \
             -I"$REPO_ROOT/vstd/include" \
             "$REPO_ROOT/examples/$standalone_ex" \
             -o "$TMP_SEALED_BIN"

    if nm "$TMP_SEALED_BIN" | grep -qE "objc_msgSend|objc_msg_lookup|__objc_exec_class|_OBJC_CLASS_|_OBJC_METACLASS_|[[:space:]]U[[:space:]]+malloc|[[:space:]]U[[:space:]]+free"; then
        echo "[FAIL] $standalone_ex linked binary contains forbidden ObjC runtime or heap symbols!"
        nm "$TMP_SEALED_BIN"
        exit 1
    fi

    "$TMP_SEALED_BIN"
    echo "[PASS] $standalone_ex linked without -lobjc and executed end-to-end cleanly."
done

# 4. Verify real Z3 SMT contract proof on examples/vcontracts_bounds_proof.m
echo "[RUN] Z3 SMT Contract Proof Example: vcontracts_bounds_proof.m"
Z3_EX_OUT=$(clang-23 -std=gnu23 -O2 -Werror -fblocks \
                     -Xclang -load -Xclang "$PLUGIN_PATH" \
                     -Xclang -add-plugin -Xclang veriobjc-check \
                     -Xclang -plugin-arg-veriobjc-check -Xclang --smt-verify \
                     -Xclang -plugin-arg-veriobjc-check -Xclang --smt-report \
                     -I"$REPO_ROOT/rt/include" \
                     -I"$REPO_ROOT/vstd/include" \
                     -fsyntax-only "$REPO_ROOT/examples/vcontracts_bounds_proof.m" 2>&1)
for proven_fn in "clamp_limit_price" "round_up_dma_frame_64"; do
    if ! echo "$Z3_EX_OUT" | grep -q "Function: $proven_fn | .*Solver: Z3 version .* | Status: UNSAT_PROVEN"; then
        echo "[FAIL] Expected Z3 to prove $proven_fn UNSAT_PROVEN in vcontracts_bounds_proof.m!"
        echo "$Z3_EX_OUT"
        exit 1
    fi
done
echo "[PASS] vcontracts_bounds_proof.m verified UNSAT_PROVEN by real Z3."

echo "=== All Examples Smoke & Acceptance Tests Passed! ==="
exit 0
