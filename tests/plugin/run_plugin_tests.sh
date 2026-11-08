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

# 1. Positive Tests (Must pass and emit ZERO objc_msgSend / objc_msg_lookup references)
TMP_OBJ=$(mktemp /tmp/veriobjc_pos_XXXXXX.o)
trap 'rm -f "$TMP_OBJ"' EXIT

for pos_file in "$REPO_ROOT"/tests/plugin/positive/*.m; do
    echo "[RUN] Positive Test: $(basename "$pos_file")"
    clang-23 -fblocks \
             -Xclang -load -Xclang "$PLUGIN_PATH" \
             -Xclang -add-plugin -Xclang veriobjc-check \
             -I"$REPO_ROOT/rt/include" \
             -c "$pos_file" \
             -o "$TMP_OBJ"
    if nm "$TMP_OBJ" | grep -qE "objc_msgSend|objc_msg_lookup|objc_enumerationMutation|_NSConcreteStackBlock|_NSConcreteGlobalBlock|_Block_object_assign"; then
        echo "[FAIL] Positive Test $(basename "$pos_file") emitted forbidden dynamic dispatch or block runtime symbol!"
        nm "$TMP_OBJ"
        exit 1
    fi
    if [[ "$(basename "$pos_file")" == "test_valid_range_for.m" ]]; then
        AST_OUT=$(clang-23 -Xclang -load -Xclang "$PLUGIN_PATH" -Xclang -add-plugin -Xclang veriobjc-check -I"$REPO_ROOT/rt/include" -Xclang -ast-dump -fsyntax-only "$pos_file")
        if echo "$AST_OUT" | grep -q "ObjCForCollectionStmt"; then
            echo "[FAIL] ObjCForCollectionStmt was not lowered in test_valid_range_for.m!"
            exit 1
        fi
        if ! echo "$AST_OUT" | grep -q "__veriobjc_it"; then
            echo "[FAIL] Expected lowered pointer iterator __veriobjc_it in AST of test_valid_range_for.m!"
            exit 1
        fi
    fi
    echo "[PASS] Positive Test $(basename "$pos_file") succeeded (0 dynamic dispatch calls)."
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
    elif [[ "$base_name" == test_banned_unresolved_msg* ]]; then
        EXPECTED_MSG="VeriObjC Violation: Unresolved selector"
    elif [[ "$base_name" == test_banned_unsealed_inheritance* ]]; then
        EXPECTED_MSG="VeriObjC Violation: Polymorphic class"
    elif [[ "$base_name" == test_banned_final_inheritance* ]]; then
        EXPECTED_MSG="VeriObjC Violation: Cannot inherit from final class"
    elif [[ "$base_name" == test_banned_unsealed_protocol* ]]; then
        EXPECTED_MSG="VeriObjC Violation: Protocol"
    elif [[ "$base_name" == test_banned_use_after_move* ]] || [[ "$base_name" == test_banned_double_free* ]]; then
        EXPECTED_MSG="VeriObjC Violation: Use of consumed linear variable"
    elif [[ "$base_name" == test_banned_escaping_stack_ref* ]]; then
        EXPECTED_MSG="VeriObjC Violation: Escaping data-stack reference"
    elif [[ "$base_name" == test_banned_nonflat_channel* ]]; then
        EXPECTED_MSG="VeriObjC Violation: Non-flat channel payload type"
    elif [[ "$base_name" == test_banned_unbounded_for_in* ]]; then
        EXPECTED_MSG="VeriObjC Violation: Range-based for loop requires a container with a statically bounded array field"
    elif [[ "$base_name" == test_banned_uncaught_err_from* ]]; then
        EXPECTED_MSG="VeriObjC Violation: Uncaught multi-frame error return"
    elif [[ "$base_name" == test_banned_escaping_block* ]]; then
        EXPECTED_MSG="VeriObjC Violation: Escaping function block is banned"
    elif [[ "$base_name" == test_banned_unconstrained_generic* ]]; then
        EXPECTED_MSG="VeriObjC Violation: Generic type parameter"
    elif [[ "$base_name" == test_banned_vconstexpr_fuel* ]]; then
        EXPECTED_MSG="VeriObjC Violation: vconstexpr evaluation"
    fi

    OUTPUT=$(clang-23 -fblocks \
                      -Xclang -load -Xclang "$PLUGIN_PATH" \
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
