#!/usr/bin/env bash
set -euo pipefail

PLUGIN_PATH="${1:-}"
REPO_ROOT="${2:-}"

if [ -z "$REPO_ROOT" ]; then
    SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
    REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
fi

if [ -z "$PLUGIN_PATH" ]; then
    PLUGIN_PATH="$REPO_ROOT/build/libVeriObjCPlugin.so"
fi

if [ ! -f "$PLUGIN_PATH" ]; then
    echo "ERROR: Plugin binary not found at $PLUGIN_PATH"
    exit 1
fi

SRC_FILE="$REPO_ROOT/tests/integration/test_e2e_sealed_polymorphism.m"
MIXED_SRC_FILE="$REPO_ROOT/tests/integration/test_e2e_sealed_mixed_scope.m"
TMP_OBJ=$(mktemp /tmp/veriobjc_sealed_e2e_XXXXXX.o)
TMP_BIN_O0=$(mktemp /tmp/veriobjc_sealed_e2e_o0_XXXXXX)
TMP_BIN_O2=$(mktemp /tmp/veriobjc_sealed_e2e_o2_XXXXXX)
TMP_MIXED_OBJ=$(mktemp /tmp/veriobjc_sealed_mixed_XXXXXX.o)
TMP_MIXED_BIN_O0=$(mktemp /tmp/veriobjc_sealed_mixed_o0_XXXXXX)
TMP_MIXED_BIN_O2=$(mktemp /tmp/veriobjc_sealed_mixed_o2_XXXXXX)
trap 'rm -f "$TMP_OBJ" "$TMP_BIN_O0" "$TMP_BIN_O2" "$TMP_MIXED_OBJ" "$TMP_MIXED_BIN_O0" "$TMP_MIXED_BIN_O2"' EXIT

echo "=== VeriObjC End-to-End Freestanding @sealed Polymorphism Gate ==="
echo "Plugin: $PLUGIN_PATH"
echo "Source: $SRC_FILE"

# 1. Compile object at -O0 and verify synthesized dispatch + zero ObjC runtime symbols
clang-23 -std=gnu23 -O0 -Wall -Wextra -Werror -Wno-objc-root-class \
         -Xclang -load -Xclang "$PLUGIN_PATH" \
         -Xclang -add-plugin -Xclang veriobjc-check \
         -fpass-plugin="$PLUGIN_PATH" \
         -I"$REPO_ROOT/rt/include" \
         -c "$SRC_FILE" \
         -o "$TMP_OBJ"

if nm "$TMP_OBJ" | grep -qE "objc_msgSend|objc_msg_lookup|__objc_exec_class|_OBJC_CLASS_|_OBJC_METACLASS_|malloc|free"; then
    echo "[FAIL] Object file emitted forbidden Objective-C runtime or heap symbols!"
    nm "$TMP_OBJ"
    exit 1
fi

if ! nm "$TMP_OBJ" | grep -q "__veriobjc_dispatch_SealedComputeNode__compute"; then
    echo "[FAIL] Expected static tag-switched dispatch function __veriobjc_dispatch_SealedComputeNode__compute in -O0 object!"
    nm "$TMP_OBJ"
    exit 1
fi

# 2. Link & run native executable WITHOUT -lobjc at -O0
clang-23 -std=gnu23 -O0 -Wall -Wextra -Werror -Wno-objc-root-class \
         -Xclang -load -Xclang "$PLUGIN_PATH" \
         -Xclang -add-plugin -Xclang veriobjc-check \
         -I"$REPO_ROOT/rt/include" \
         "$SRC_FILE" \
         -o "$TMP_BIN_O0"

"$TMP_BIN_O0"
echo "[PASS] -O0 standalone executable linked without -lobjc and passed runtime polymorphism checks."

# 3. Link & run native executable WITHOUT -lobjc at -O2 (with -fpass-plugin enabled too)
clang-23 -std=gnu23 -O2 -Wall -Wextra -Werror -Wno-objc-root-class \
         -Xclang -load -Xclang "$PLUGIN_PATH" \
         -Xclang -add-plugin -Xclang veriobjc-check \
         -fpass-plugin="$PLUGIN_PATH" \
         -I"$REPO_ROOT/rt/include" \
         "$SRC_FILE" \
         -o "$TMP_BIN_O2"

"$TMP_BIN_O2"

# 4. Audit Trifecta on linked -O2 executable: nm + llvm-objdump disassembly
if nm "$TMP_BIN_O2" | grep -qE "objc_msgSend|objc_msg_lookup|__objc_exec_class|_OBJC_CLASS_|_OBJC_METACLASS_|[[:space:]]U[[:space:]]+malloc|[[:space:]]U[[:space:]]+free"; then
    echo "[FAIL] Linked -O2 executable contains forbidden Objective-C runtime or heap allocator symbols!"
    nm "$TMP_BIN_O2"
    exit 1
fi

DISASM_O2=$(llvm-objdump-23 -d "$TMP_BIN_O2")
if echo "$DISASM_O2" | grep -qE "objc_msgSend|objc_msg_lookup|__objc_exec_class"; then
    echo "[FAIL] Disassembly contains dynamic Objective-C runtime call!"
    exit 1
fi

if ! echo "$DISASM_O2" | grep -A 35 "<eval_polymorphic_compute>:" | grep -qE "cmp|je|jne|jb|ja"; then
    echo "[FAIL] Expected static tag comparison/switch in eval_polymorphic_compute disassembly!"
    echo "$DISASM_O2" | grep -A 35 "<eval_polymorphic_compute>:"
    exit 1
fi

echo "[PASS] -O2 standalone executable linked without -lobjc, passed nm zero-runtime audit, and verified static tag-switch disassembly."

# 5. Task 5.5 Mixed-Size @sealed Hierarchy + vspan + One-Store Scope Reset Composition Gate
echo "[RUN] Mixed-Size @sealed Composition Test: $(basename "$MIXED_SRC_FILE")"

clang-23 -std=gnu23 -O0 -Wall -Wextra -Werror -Wno-objc-root-class \
         -Xclang -load -Xclang "$PLUGIN_PATH" \
         -Xclang -add-plugin -Xclang veriobjc-check \
         -fpass-plugin="$PLUGIN_PATH" \
         -I"$REPO_ROOT/rt/include" \
         -I"$REPO_ROOT/vstd/include" \
         -c "$MIXED_SRC_FILE" \
         -o "$TMP_MIXED_OBJ"

if nm "$TMP_MIXED_OBJ" | grep -qE "objc_msgSend|objc_msg_lookup|__objc_exec_class|_OBJC_CLASS_|_OBJC_METACLASS_|malloc|free"; then
    echo "[FAIL] Mixed-size object file emitted forbidden Objective-C runtime or heap symbols!"
    nm "$TMP_MIXED_OBJ"
    exit 1
fi

for sym in "__veriobjc_dispatch_TelemetryFrameNode__computeDigest" "__veriobjc_dispatch_TelemetryFrameNode__declaredPayloadBytes"; do
    if ! nm "$TMP_MIXED_OBJ" | grep -q "$sym"; then
        echo "[FAIL] Expected synthesized static dispatch symbol $sym in mixed-size -O0 object!"
        nm "$TMP_MIXED_OBJ"
        exit 1
    fi
done

clang-23 -std=gnu23 -O0 -Wall -Wextra -Werror -Wno-objc-root-class \
         -Xclang -load -Xclang "$PLUGIN_PATH" \
         -Xclang -add-plugin -Xclang veriobjc-check \
         -I"$REPO_ROOT/rt/include" \
         -I"$REPO_ROOT/vstd/include" \
         "$MIXED_SRC_FILE" \
         -o "$TMP_MIXED_BIN_O0"
"$TMP_MIXED_BIN_O0"

clang-23 -std=gnu23 -O2 -Wall -Wextra -Werror -Wno-objc-root-class \
         -Xclang -load -Xclang "$PLUGIN_PATH" \
         -Xclang -add-plugin -Xclang veriobjc-check \
         -fpass-plugin="$PLUGIN_PATH" \
         -I"$REPO_ROOT/rt/include" \
         -I"$REPO_ROOT/vstd/include" \
         "$MIXED_SRC_FILE" \
         -o "$TMP_MIXED_BIN_O2"
"$TMP_MIXED_BIN_O2"

if nm "$TMP_MIXED_BIN_O2" | grep -qE "objc_msgSend|objc_msg_lookup|__objc_exec_class|_OBJC_CLASS_|_OBJC_METACLASS_|[[:space:]]U[[:space:]]+malloc|[[:space:]]U[[:space:]]+free"; then
    echo "[FAIL] Mixed-size -O2 executable contains forbidden Objective-C runtime or heap symbols!"
    nm "$TMP_MIXED_BIN_O2"
    exit 1
fi

MIXED_DISASM=$(llvm-objdump-23 -d "$TMP_MIXED_BIN_O2")
if echo "$MIXED_DISASM" | grep -qE "objc_msgSend|objc_msg_lookup|__objc_exec_class"; then
    echo "[FAIL] Mixed-size disassembly contains dynamic Objective-C runtime call!"
    exit 1
fi

# Verify one-store scope reset: mixed_scope_epilogue_reset must compile to a single movq store + retq
RESET_BODY=$(echo "$MIXED_DISASM" | awk '/<mixed_scope_epilogue_reset>:/,/^$/' | grep -E "^[[:space:]]+[0-9a-f]+:")
STORE_COUNT=$(echo "$RESET_BODY" | grep -c "movq" || true)
if [[ "$STORE_COUNT" -ne 1 ]]; then
    echo "[FAIL] Expected exactly 1 movq store in mixed_scope_epilogue_reset, got $STORE_COUNT!"
    echo "$RESET_BODY"
    exit 1
fi

# Also verify run_mixed_sealed_scope_batch restores stack->top (offset 0x8) via movq on scope exit
if ! echo "$MIXED_DISASM" | awk '/<run_mixed_sealed_scope_batch>:/,/^$/' | grep -qE "movq.*, 0x8\(%r"; then
    echo "[FAIL] Expected VERIOBJC_SCOPED_STACK in run_mixed_sealed_scope_batch to restore stack->top (0x8) via movq!"
    echo "$MIXED_DISASM" | awk '/<run_mixed_sealed_scope_batch>:/,/^$/'
    exit 1
fi

echo "[PASS] Mixed-size @sealed composition test passed (-O0/-O2 runtime, 0 ObjC/heap symbols, 1-store movq scope reset verified)."
echo "=== All Freestanding @sealed Polymorphism Checks Passed! ==="
