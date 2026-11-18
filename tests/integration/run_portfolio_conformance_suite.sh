#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="${1:-.}"
echo "=== VeriObjC Task 5.8: Portfolio Golden-Tape Conformance Suite ==="
echo "Repository Root: $REPO_ROOT"

TMP_SRC=$(mktemp /tmp/veriobjc_portfolio_conf_XXXXXX.c)
TMP_BIN=$(mktemp /tmp/veriobjc_portfolio_conf_bin_XXXXXX)
trap 'rm -f "$TMP_SRC" "$TMP_BIN"' EXIT

cat > "$TMP_SRC" << 'EOF'
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "rt/portfolio_conformance.h"

static uint8_t *load_file(const char *path, size_t *out_len) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (sz > 0 && fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        fclose(fp);
        free(buf);
        return NULL;
    }
    fclose(fp);
    *out_len = (size_t)sz;
    return buf;
}

int main(int argc, char **argv) {
    if (argc < 5) {
        fprintf(stderr, "Usage: %s <consumer_id:1|2|3> <tape_path> <expected_err_int> <expect_pass:0|1>\n", argv[0]);
        return 2;
    }
    enum vtpe_portfolio_consumer consumer = (enum vtpe_portfolio_consumer)atoi(argv[1]);
    const char *tape_path = argv[2];
    vtpe_v1_error_t expected_err = (vtpe_v1_error_t)atoi(argv[3]);
    int expect_pass = atoi(argv[4]);

    size_t len = 0;
    uint8_t *buf = load_file(tape_path, &len);
    if (!buf) {
        fprintf(stderr, "[ERROR] Unable to read %s\n", tape_path);
        return 2;
    }

    struct vtpe_conformance_report rep;
    int ok = vtpe_portfolio_verify_fixture(consumer, buf, len, expected_err, &rep);
    free(buf);

    if (!ok) {
        printf("[CONFORMANCE_FAILURE] repo=%s clause=%s actual=%u expected=%u file=%s\n",
               rep.repo_name, rep.violated_clause, (unsigned)rep.actual_error, (unsigned)rep.expected_error, tape_path);
        return (expect_pass == 0) ? 0 : 1;
    }

    printf("[CONFORMANCE_OK] repo=%s clause=%s acc=[%d,%d] scaled=[%d,%d] file=%s\n",
           rep.repo_name, rep.violated_clause, rep.oracle_acc0, rep.oracle_acc1,
           rep.oracle_scaled0, rep.oracle_scaled1, tape_path);
    return (expect_pass == 1) ? 0 : 1;
}
EOF

clang-23 -std=gnu23 -O2 -Wall -Wextra -Werror -I"$REPO_ROOT/rt/include" "$TMP_SRC" -o "$TMP_BIN"

# 12 golden fixtures: path | expected_core_err | expected_extended_err
FIXTURES=(
    "fixtures/vtpe_v1/accept/core_minimal_dot4.bin:0:0"
    "fixtures/vtpe_v1/accept/core_with_manifest_and_links.bin:0:0"
    "fixtures/vtpe_v1/accept/extended_with_fp32_aux.bin:7:0"
    "fixtures/vtpe_v1/reject/bad_magic.bin:2:2"
    "fixtures/vtpe_v1/reject/unknown_flags_bit3.bin:6:6"
    "fixtures/vtpe_v1/reject/core_reader_on_extended.bin:7:0"
    "fixtures/vtpe_v1/reject/fp32_aux_in_core_profile.bin:13:13"
    "fixtures/vtpe_v1/reject/malformed_fp32_aux_nan.bin:7:22"
    "fixtures/vtpe_v1/reject/missing_required_section.bin:7:14"
    "fixtures/vtpe_v1/reject/bad_section_checksum.bin:5:5"
    "fixtures/vtpe_v1/reject/unknown_section_tag.bin:11:11"
    "fixtures/vtpe_v1/reject/reserved_ternary_code_0b11.bin:17:17"
)

for entry in "${FIXTURES[@]}"; do
    IFS=':' read -r rel_path exp_core exp_ext <<< "$entry"
    full_path="$REPO_ROOT/$rel_path"
    # 1 = setun (Extended-capable via veriStream runtime slice + RTL sim)
    "$TMP_BIN" 1 "$full_path" "$exp_ext" 1
    # 2 = veriobjc-inference-appliance-kit (Core-only per ADR-0002)
    "$TMP_BIN" 2 "$full_path" "$exp_core" 1
    # 3 = TernaryTape.jl reference interpreter (Extended-capable)
    "$TMP_BIN" 3 "$full_path" "$exp_ext" 1
done

echo "[PASS] All 12 golden tapes verified across setun, veriobjc-inference-appliance-kit, and TernaryTape.jl."

# Negative Self-Tests: Verify that any violation names the failing repo and exact violated clause
OUT_SETUN=$("$TMP_BIN" 1 "$REPO_ROOT/fixtures/vtpe_v1/reject/bad_magic.bin" 0 0)
if ! echo "$OUT_SETUN" | grep -q "repo=setun clause=§1.header_magic"; then
    echo "[FAIL] Negative self-test failed for setun clause attribution: $OUT_SETUN"
    exit 1
fi

OUT_KIT=$("$TMP_BIN" 2 "$REPO_ROOT/fixtures/vtpe_v1/reject/core_reader_on_extended.bin" 0 0)
if ! echo "$OUT_KIT" | grep -q "repo=veriobjc-inference-appliance-kit clause=§1.flags_adr0002_profile_bit2"; then
    echo "[FAIL] Negative self-test failed for veriobjc-inference-appliance-kit clause attribution: $OUT_KIT"
    exit 1
fi

OUT_JL=$("$TMP_BIN" 3 "$REPO_ROOT/fixtures/vtpe_v1/reject/malformed_fp32_aux_nan.bin" 0 0)
if ! echo "$OUT_JL" | grep -q "repo=TernaryTape.jl clause=§6.fp32_aux_schema"; then
    echo "[FAIL] Negative self-test failed for TernaryTape.jl clause attribution: $OUT_JL"
    exit 1
fi

echo "[PASS] Negative self-tests verified: failures explicitly report repo=<repo> and clause=<violated_clause>."
echo "=== All Task 5.8 Portfolio Golden-Tape Conformance Checks Passed! ==="
exit 0
