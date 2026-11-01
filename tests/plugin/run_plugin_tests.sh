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

echo "=== VeriObjC Plugin Test Runner ==="
echo "Testing Plugin: $PLUGIN_PATH"
echo "Repository Root: $REPO_ROOT"

# 1. Positive Tests (Must pass)
echo "[RUN] Positive Test: test_valid_nominals.m"
clang-23 -Xclang -load -Xclang "$PLUGIN_PATH" \
         -Xclang -add-plugin -Xclang veriobjc-check \
         -I"$REPO_ROOT/rt/include" \
         -c "$REPO_ROOT/tests/plugin/positive/test_valid_nominals.m" \
         -o /dev/null
echo "[PASS] Positive Test succeeded."

# 2. Negative Tests (Must fail with specific diagnostic)
for test_file in "$REPO_ROOT"/tests/plugin/negative/*.m; do
    echo "[RUN] Negative Test: $test_file"
    OUTPUT=$(clang-23 -Xclang -load -Xclang "$PLUGIN_PATH" \
                      -Xclang -add-plugin -Xclang veriobjc-check \
                      -I"$REPO_ROOT/rt/include" \
                      -c "$test_file" \
                      -o /dev/null 2>&1 || true)

    if echo "$OUTPUT" | grep -q "VeriObjC Violation: Banned use of generic 'id' type"; then
        echo "[PASS] Successfully caught violation in $(basename "$test_file")"
    else
        echo "[FAIL] Negative Test did NOT produce expected error diagnostic in $test_file!"
        echo "Compiler output:"
        echo "$OUTPUT"
        exit 1
    fi
done

echo "=== All Plugin Tests Passed! ==="
exit 0
