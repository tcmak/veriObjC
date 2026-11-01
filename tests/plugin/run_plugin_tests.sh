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
for pos_file in "$REPO_ROOT"/tests/plugin/positive/*.m; do
    echo "[RUN] Positive Test: $(basename "$pos_file")"
    clang-23 -Xclang -load -Xclang "$PLUGIN_PATH" \
             -Xclang -add-plugin -Xclang veriobjc-check \
             -I"$REPO_ROOT/rt/include" \
             -c "$pos_file" \
             -o /dev/null
    echo "[PASS] Positive Test $(basename "$pos_file") succeeded."
done

# 2. Negative Tests (Must fail with specific diagnostic)
for test_file in "$REPO_ROOT"/tests/plugin/negative/*.m; do
    base_name=$(basename "$test_file")
    echo "[RUN] Negative Test: $base_name"

    EXPECTED_MSG=""
    if [[ "$base_name" == test_banned_id* ]]; then
        EXPECTED_MSG="VeriObjC Violation: Banned use of generic 'id' type"
    elif [[ "$base_name" == test_banned_nsobject* ]]; then
        EXPECTED_MSG="VeriObjC Violation: Banned inheritance from 'NSObject'"
    elif [[ "$base_name" == test_banned_reflection* ]]; then
        EXPECTED_MSG="VeriObjC Violation: Banned use of dynamic runtime reflection"
    elif [[ "$base_name" == test_banned_untyped_msg* ]]; then
        EXPECTED_MSG="VeriObjC Violation: Dynamic message dispatch requires an explicitly typed receiver"
    fi

    OUTPUT=$(clang-23 -Xclang -load -Xclang "$PLUGIN_PATH" \
                      -Xclang -add-plugin -Xclang veriobjc-check \
                      -I"$REPO_ROOT/rt/include" \
                      -c "$test_file" \
                      -o /dev/null 2>&1 || true)

    if echo "$OUTPUT" | grep -q "$EXPECTED_MSG"; then
        echo "[PASS] Successfully caught violation in $base_name"
    else
        echo "[FAIL] Negative Test did NOT produce expected error diagnostic in $base_name!"
        echo "Expected: $EXPECTED_MSG"
        echo "Compiler output:"
        echo "$OUTPUT"
        exit 1
    fi
done

echo "=== All Plugin Tests Passed! ==="
exit 0
