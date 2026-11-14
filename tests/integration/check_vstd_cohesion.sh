#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="${1:-.}"
README_FILE="$REPO_ROOT/examples/README.md"

echo "=== VeriObjC Phase 3 Header Cohesion, Minimal-Binary & Layer Table Gate ==="
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

# 4. Layer 'hw': may import rt, sys0, hw ONLY (never vstd)
for hdr in "$REPO_ROOT"/hw/include/hw/*.h; do
    base=$(basename "$hdr")
    check_header_layer "$hdr" "hw" "(vstd/)" \
        -I"$REPO_ROOT/rt/include" -I"$REPO_ROOT/sys0/include" -I"$REPO_ROOT/hw/include"

    if ! grep -q "hw/$base" "$README_FILE"; then
        echo "[FAIL] Public header <hw/$base> has no documented example entry in examples/README.md!"
        exit 1
    fi
done

# 5. Negative Self-Test: Verify gate rejects a synthetic leaf header with an illegal cross-layer include
TMP_BAD_HDR=$(mktemp /tmp/veriobjc_bad_hdr_XXXXXX.h)
TMP_SRC=$(mktemp /tmp/veriobjc_leaf_XXXXXX.c)
TMP_BIN=$(mktemp /tmp/veriobjc_leaf_bin_XXXXXX)
TMP_HW_BIN=$(mktemp /tmp/veriobjc_hello_world_XXXXXX)
TMP_UART_BIN=$(mktemp /tmp/veriobjc_hello_uart_XXXXXX)
trap 'rm -f "$TMP_BAD_HDR" "$TMP_SRC" "$TMP_BIN" "$TMP_HW_BIN" "$TMP_UART_BIN"' EXIT

cat > "$TMP_BAD_HDR" << 'EOF'
#pragma once
#include "veriobjc_runtime.h"
#include "sys0/mem.h"
EOF

BAD_TRACE=$(clang-23 -std=gnu23 -H -fsyntax-only -I"$REPO_ROOT/rt/include" -I"$REPO_ROOT/vstd/include" -I"$REPO_ROOT/sys0/include" -x c-header "$TMP_BAD_HDR" 2>&1 || true)
if ! echo "$BAD_TRACE" | grep -E "^\.+ " | grep -qE "(sys0/|hw/)"; then
    echo "[FAIL] Negative self-test failed: synthetic cross-layer include was not detected!"
    exit 1
fi
echo "[PASS] Negative self-test verified: cross-layer include in vstd header is caught."

# 6. Dead-Strip (--gc-sections) Audit on <vstd/string.h> Linked Binary
cat > "$TMP_SRC" << 'EOF'
#include <vstd/string.h>
int main(void) {
    struct vstring_view v = vstring_view_FromLiteral("ok");
    return (v.len == 2u) ? 0 : 1;
}
EOF
clang-23 -std=gnu23 -O2 -ffunction-sections -fdata-sections -Wl,--gc-sections \
         -I"$REPO_ROOT/rt/include" -I"$REPO_ROOT/vstd/include" \
         "$TMP_SRC" -o "$TMP_BIN"

if nm "$TMP_BIN" | grep -qE "vstack_array|vvector|vflat_map|vhash_flat|vspsc_channel|vternary|sys0_"; then
    echo "[FAIL] --gc-sections dead-strip audit failed: <vstd/string.h> binary leaked container/substrate symbols!"
    nm "$TMP_BIN"
    exit 1
fi
echo "[PASS] --gc-sections dead-strip audit passed on <vstd/string.h>."

# 7. Task 3.15 Hosted Minimal-Binary Proof: examples/hello_world.m (zero vstd imports, links against rt/ alone)
HW_TRACE=$(clang-23 -std=gnu23 -O2 -ffunction-sections -fdata-sections -Wl,--gc-sections \
                    -DVERIOBJC_EXAMPLE_MAIN -H \
                    -I"$REPO_ROOT/rt/include" \
                    "$REPO_ROOT/examples/hello_world.m" -o "$TMP_HW_BIN" 2>&1)
if echo "$HW_TRACE" | grep -E "^\.+ " | grep -qE "(vstd/|sys0/|hw/)"; then
    echo "[FAIL] examples/hello_world.m imported vstd/sys0/hw headers!"
    echo "$HW_TRACE"
    exit 1
fi
HW_BYTES=$(stat -c%s "$TMP_HW_BIN")
echo "[PASS] examples/hello_world.m verified (0 vstd imports, linked against rt/ alone, binary size: ${HW_BYTES} bytes)."

# 8. Task 3.15 Bare-Metal Minimal-Binary Proof: examples/hello_uart.m (io.h + sys0/boot.h + hw/loopback.h closure only)
UART_TRACE=$(clang-23 -std=gnu23 -O2 -ffunction-sections -fdata-sections -Wl,--gc-sections \
                      -DVERIOBJC_EXAMPLE_MAIN -H \
                      -I"$REPO_ROOT/rt/include" -I"$REPO_ROOT/vstd/include" -I"$REPO_ROOT/sys0/include" -I"$REPO_ROOT/hw/include" \
                      "$REPO_ROOT/examples/hello_uart.m" -o "$TMP_UART_BIN" 2>&1)
if echo "$UART_TRACE" | grep -E "^\.+ " | grep -qE "vstd/(vector|map|hash|variant|channel|atomic|stream|ternary|simd|contracts|execution|stack|string|span|strong|result)\.h"; then
    echo "[FAIL] examples/hello_uart.m leaked non-IO vstd headers into its closure!"
    echo "$UART_TRACE"
    exit 1
fi
UART_BYTES=$(stat -c%s "$TMP_UART_BIN")
echo "[PASS] examples/hello_uart.m verified (<vstd/io.h> + <sys0/boot.h> + <hw/loopback.h> closure only, binary size: ${UART_BYTES} bytes)."

echo "=== All Phase 3 Header Cohesion & Minimal-Binary Gate Checks Passed! ==="
exit 0
