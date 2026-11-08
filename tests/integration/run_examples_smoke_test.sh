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
trap 'rm -f "$TMP_OBJ"' EXIT

# 1. Compile and audit Objective-C (.m) examples
for m_file in "$REPO_ROOT"/examples/*.m; do
    base_name=$(basename "$m_file")
    echo "[RUN] Objective-C Example: $base_name"

    clang-23 -std=gnu23 -O2 -Werror -fblocks \
             -Xclang -load -Xclang "$PLUGIN_PATH" \
             -Xclang -add-plugin -Xclang veriobjc-check \
             -I"$REPO_ROOT/rt/include" \
             -c "$m_file" \
             -o "$TMP_OBJ"

    # Verify zero dynamic dispatch or block runtime references
    if nm "$TMP_OBJ" | grep -qE "objc_msgSend|objc_msg_lookup|objc_enumerationMutation|_NSConcreteStackBlock|_NSConcreteGlobalBlock|_Block_object_assign"; then
        echo "[FAIL] $base_name emitted forbidden dynamic dispatch or block runtime symbol!"
        nm "$TMP_OBJ"
        exit 1
    fi

    # Verify zero heap allocator references (malloc / free)
    if nm "$TMP_OBJ" | grep -qE "U malloc|U free"; then
        echo "[FAIL] $base_name emitted forbidden heap allocator reference!"
        nm "$TMP_OBJ"
        exit 1
    fi

    if [[ "$base_name" == "ternary_glu_block.m" ]]; then
        HW_OUT=$(clang-23 -std=gnu23 -fblocks -Xclang -load -Xclang "$PLUGIN_PATH" -Xclang -add-plugin -Xclang veriobjc-check -Xclang -plugin-arg-veriobjc-check -Xclang --hw-report -I"$REPO_ROOT/rt/include" -fsyntax-only "$m_file" 2>&1)
        if ! echo "$HW_OUT" | grep -q "DSPs: 0 | ReduceStages: 9 | Status: PASS"; then
            echo "[FAIL] ternary_glu_block.m failed --hw-report zero-DSP synthesis gate!"
            echo "$HW_OUT"
            exit 1
        fi
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

echo "=== All Examples Smoke & Acceptance Tests Passed! ==="
exit 0
