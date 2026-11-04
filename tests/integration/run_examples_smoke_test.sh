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

    clang-23 -O2 -Werror \
             -Xclang -load -Xclang "$PLUGIN_PATH" \
             -Xclang -add-plugin -Xclang veriobjc-check \
             -I"$REPO_ROOT/rt/include" \
             -c "$m_file" \
             -o "$TMP_OBJ"

    # Verify zero dynamic dispatch references (objc_msgSend / objc_msg_lookup)
    if nm "$TMP_OBJ" | grep -qE "objc_msgSend|objc_msg_lookup"; then
        echo "[FAIL] $base_name emitted forbidden dynamic dispatch symbol (objc_msgSend / objc_msg_lookup)!"
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
