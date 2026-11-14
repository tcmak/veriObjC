#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="${1:-.}"
README_FILE="$REPO_ROOT/examples/README.md"

echo "=== VeriObjC Phase 3 Header Cohesion & Layer Table Gate ==="
echo "Repository Root: $REPO_ROOT"

if [ ! -f "$README_FILE" ]; then
    echo "[FAIL] Missing $README_FILE!"
    exit 1
fi

check_header_layer() {
    local hdr_path="$1"
    local layer_name="$2"
    local forbidden_regex="$3"
    shift 3
    local include_flags=("$@")

    local rel_hdr="${hdr_path#"$REPO_ROOT"/}"
    echo "[COHESION] Checking $rel_hdr (layer: $layer_name)"

    local trace_out
    if ! trace_out=$(clang-23 -std=gnu23 -Wall -Wextra -Werror -H -fsyntax-only "${include_flags[@]}" -x c-header "$hdr_path" 2>&1); then
        echo "[FAIL] Header $rel_hdr failed standalone compilation under layer '$layer_name' include roots!"
        echo "$trace_out"
        exit 1
    fi

    if [ -n "$forbidden_regex" ] && echo "$trace_out" | grep -E "^\.+ " | grep -qE "$forbidden_regex"; then
        echo "[FAIL] Layer violation in $rel_hdr (layer '$layer_name' imported forbidden dependency matching '$forbidden_regex')!"
        echo "$trace_out" | grep -E "^\.+ "
        exit 1
    fi
}

# 1. Layer 'rt': may import nothing from vstd, sys0, hw
check_header_layer "$REPO_ROOT/rt/include/veriobjc_runtime.h" "rt" "(vstd/|sys0/|hw/)" \
    -I"$REPO_ROOT/rt/include"

# 2. Layer 'vstd': may import rt and vstd ONLY (never sys0 or hw)
shopt -s nullglob
for hdr in "$REPO_ROOT"/vstd/include/vstd/*.h; do
    base=$(basename "$hdr")
    check_header_layer "$hdr" "vstd" "(sys0/|hw/)" \
        -I"$REPO_ROOT/rt/include" -I"$REPO_ROOT/vstd/include"

    if ! grep -q "vstd/$base" "$README_FILE"; then
        echo "[FAIL] Public header <vstd/$base> has no documented example entry in examples/README.md!"
        exit 1
    fi
done

# 3. Layer 'sys0': may import rt and sys0 ONLY (never vstd or hw)
for hdr in "$REPO_ROOT"/sys0/include/sys0/*.h; do
    base=$(basename "$hdr")
    check_header_layer "$hdr" "sys0" "(vstd/|hw/)" \
        -I"$REPO_ROOT/rt/include" -I"$REPO_ROOT/sys0/include"

    if ! grep -q "sys0/$base" "$README_FILE"; then
        echo "[FAIL] Public header <sys0/$base> has no documented example entry in examples/README.md!"
        exit 1
    fi
done

# 4. Layer 'hw' (when present): may import rt, sys0, hw ONLY (never vstd)
for hdr in "$REPO_ROOT"/hw/include/hw/*.h; do
    base=$(basename "$hdr")
    check_header_layer "$hdr" "hw" "(vstd/)" \
        -I"$REPO_ROOT/rt/include" -I"$REPO_ROOT/sys0/include" -I"$REPO_ROOT/hw/include"

    if ! grep -q "hw/$base" "$README_FILE"; then
        echo "[FAIL] Public header <hw/$base> has no documented example entry in examples/README.md!"
        exit 1
    fi
done

# 5. Dead-strip / Leaf Isolation Audit for <vstd/string.h>
if [ -f "$REPO_ROOT/vstd/include/vstd/string.h" ]; then
    TMP_SRC=$(mktemp /tmp/veriobjc_leaf_XXXXXX.c)
    TMP_OBJ=$(mktemp /tmp/veriobjc_leaf_XXXXXX.o)
    trap 'rm -f "$TMP_SRC" "$TMP_OBJ"' EXIT

    cat > "$TMP_SRC" << 'EOF'
#include <vstd/string.h>
size_t leaf_len(void) {
    struct vstring_view v = vstring_view_FromLiteral("ok");
    return v.len;
}
EOF
    clang-23 -std=gnu23 -O2 -ffunction-sections -fdata-sections \
             -I"$REPO_ROOT/rt/include" -I"$REPO_ROOT/vstd/include" \
             -c "$TMP_SRC" -o "$TMP_OBJ"

    if nm "$TMP_OBJ" | grep -qE "vstack_array|vvector|veriobjc_pool|sys0_"; then
        echo "[FAIL] Leaf <vstd/string.h> translation unit leaked non-leaf container/substrate symbols!"
        nm "$TMP_OBJ"
        exit 1
    fi
    echo "[PASS] Leaf <vstd/string.h> dead-strip isolation verified (0 container/substrate symbols)."
fi

echo "=== All Header Cohesion & Layer Table Checks Passed! ==="
exit 0
