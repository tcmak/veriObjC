#!/usr/bin/env bash
set -euo pipefail

BUILD_DIR="${1:-}"
REPO_ROOT="${2:-}"

if [ -z "$REPO_ROOT" ]; then
    SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
    REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
fi
REPO_ROOT="$(cd "$REPO_ROOT" && pwd)"

if [ -z "$BUILD_DIR" ]; then
    BUILD_DIR="$REPO_ROOT/build"
fi
BUILD_DIR="$(cd "$BUILD_DIR" && pwd)"

echo "=== VeriObjC SDK Install & veriobjc-cc End-to-End Gate ==="
echo "Build Dir: $BUILD_DIR"
echo "Repo Root: $REPO_ROOT"

TMP_SDK="$(mktemp -d /tmp/veriobjc_sdk_XXXXXX)"
TMP_WORK="$(mktemp -d /tmp/veriobjc_sdk_work_XXXXXX)"
trap 'rm -rf "$TMP_SDK" "$TMP_WORK"' EXIT

# 1. Verify build-tree driver exists
if [ ! -x "$BUILD_DIR/bin/veriobjc-cc" ]; then
    echo "[FAIL] Build-tree compiler wrapper not found or not executable: $BUILD_DIR/bin/veriobjc-cc"
    exit 1
fi
"$BUILD_DIR/bin/veriobjc-cc" --version >/dev/null
"$BUILD_DIR/bin/veriobjc-cc" --help >/dev/null

# 2. Install relocatable SDK into external prefix (/tmp)
echo "[RUN] Installing VeriObjC SDK to $TMP_SDK"
cmake --install "$BUILD_DIR" --prefix "$TMP_SDK"

for req_path in \
    "$TMP_SDK/bin/veriobjc-cc" \
    "$TMP_SDK/lib/veriobjc/libVeriObjCPlugin.so" \
    "$TMP_SDK/lib/veriobjc/libveriobjc_rt.a" \
    "$TMP_SDK/include/veriobjc/rt/veriobjc_runtime.h" \
    "$TMP_SDK/include/veriobjc/rt/rt/dispatch.h" \
    "$TMP_SDK/include/veriobjc/vstd/span.h" \
    "$TMP_SDK/include/veriobjc/vstd/vector.h" \
    "$TMP_SDK/include/veriobjc/vstd/contracts.h" \
    "$TMP_SDK/include/veriobjc/sys0/mem.h" \
    "$TMP_SDK/include/veriobjc/hw/loopback.h" \
    "$TMP_SDK/include/veriobjc/host/veriobjc_host_io.h" \
    "$TMP_SDK/include/veriobjc/host/host/smt_bridge.h"
do
    if [ ! -e "$req_path" ]; then
        echo "[FAIL] Missing installed SDK artifact: $req_path"
        exit 1
    fi
done
if [ ! -x "$TMP_SDK/bin/veriobjc-cc" ]; then
    echo "[FAIL] Installed driver is not executable: $TMP_SDK/bin/veriobjc-cc"
    exit 1
fi
echo "[PASS] SDK directory layout verified under $TMP_SDK"

# 3. Compile, link & run standalone VeriObjC program from /tmp outside the repo
printf '\x4f\x56\x04\x00\x2a\x00\x00\x00\x0a\x14\x1e\x28' > "$TMP_WORK/frame.bin"

cat > "$TMP_WORK/sdk_dogfood.m" <<'EOF'
#include <veriobjc_runtime.h>
#include <rt/dispatch.h>
#include <veriobjc_host_io.h>
#include <vstd/span.h>
#include <synth_c99_frame.h>

__attribute__((objc_root_class, objc_sealed))
@interface FrameStageNode {
@public
    struct veriobjc_sealed_header _sealed_hdr;
}
- (uint32_t)transformWord:(uint32_t)word;
@end

@implementation FrameStageNode
- (uint32_t)transformWord:(uint32_t)word {
    return word;
}
@end

__attribute__((objc_sealed, objc_final))
@interface AddBiasStage : FrameStageNode {
@public
    uint32_t bias;
}
- (uint32_t)transformWord:(uint32_t)word;
@end

@implementation AddBiasStage
- (uint32_t)transformWord:(uint32_t)word {
    return word + self->bias;
}
@end

__attribute__((objc_sealed, objc_final))
@interface XorMixStage : FrameStageNode {
@public
    uint32_t mask_lo;
    uint32_t mask_hi;
    uint64_t tag_salt;
}
- (uint32_t)transformWord:(uint32_t)word;
@end

@implementation XorMixStage
- (uint32_t)transformWord:(uint32_t)word {
    return (word ^ self->mask_lo ^ self->mask_hi) + (uint32_t)self->tag_salt;
}
@end

vspan_def(ConstByteSpan, const uint8_t);
vspan_def(StageNodeSpan, FrameStageNode *);

static uint8_t g_file_staging[128];
static uint8_t g_arena_backing[512];

veriobjc_pure veriobjc_nolatency
static uint32_t sum_payload_bytes(uint32_t seq_no, ConstByteSpan payload) {
    uint32_t sum = seq_no;
    for (size_t i = 0u; i < payload.len; ++i) {
        const uint8_t *p = vspan_at(&payload, i);
        if (p) {
            sum += (uint32_t)(*p);
        }
    }
    return sum;
}

static uint32_t run_sealed_stages(struct veriobjc_stack *arena, uint32_t seed) {
    VERIOBJC_SCOPED_STACK(arena);
    AddBiasStage *s0 = veriobjc_alloc_sealed(arena, AddBiasStage);
    XorMixStage  *s1 = veriobjc_alloc_sealed(arena, XorMixStage);
    if (!s0 || !s1) {
        return 0u;
    }
    s0->bias     = 8u;
    s1->mask_lo  = 0x0Fu;
    s1->mask_hi  = 0x0Fu;
    s1->tag_salt = 2u;

    FrameStageNode *nodes[2] = { (FrameStageNode *)s0, (FrameStageNode *)s1 };
    StageNodeSpan batch = vspan_from_array(StageNodeSpan, nodes);
    uint32_t cur = seed;
    for (size_t i = 0u; i < batch.len; ++i) {
        FrameStageNode **slot = vspan_at(&batch, i);
        if (slot && *slot) {
            cur = [*slot transformWord:cur];
        }
    }
    return cur;
}

veriobjc_cold
int main(int argc, const char **argv) {
    if (argc < 2 || veriobjc_rt_abi_version() != 1u) {
        return 2;
    }

    struct veriobjc_stack arena;
    veriobjc_stack_init(&arena, g_arena_backing, sizeof(g_arena_backing));

    struct veriobjc_host_io_result staged =
        veriobjc_host_load_file_staged(argv[1], g_file_staging, sizeof(g_file_staging));
    if (!staged.is_ok) {
        return 3;
    }
    if (SYNTH_C99_READ_U16_LE(staged.value.data) != SYNTH_C99_MAGIC_V1) {
        return 4;
    }

    ConstByteSpan payload_span = { .data = NULL, .len = 0u };
    uint32_t seq_no = 0u;
    {
        VERIOBJC_SCOPED_STACK(&arena);
        struct synth_c99_frame *cursor =
            (struct synth_c99_frame *)veriobjc_stack_alloc(&arena, sizeof(*cursor));
        if (!cursor || !synth_c99_unpack_frame(staged.value.data, staged.value.len, cursor)) {
            return 5;
        }
        vspan_init(&payload_span, cursor->payload, cursor->payload_len);
        seq_no = cursor->seq_no;
    }

    // 42 + (10 + 20 + 30 + 40) = 142 -> +8 = 150 -> ^0 + 2 = 152
    uint32_t base_sum = sum_payload_bytes(seq_no, payload_span);
    uint32_t final_out = run_sealed_stages(&arena, base_sum);
    return (final_out == 152u && arena.top == arena.base) ? 0 : 6;
}
EOF

(
    cd "$TMP_WORK"
    "$TMP_SDK/bin/veriobjc-cc" -O2 -Werror \
        -isystem "$REPO_ROOT/tests/plugin/isystem_ext" \
        sdk_dogfood.m -o sdk_dogfood
    ./sdk_dogfood "$TMP_WORK/frame.bin"

    # Compile-only (-c) and preprocess (-E) with -Werror must emit zero unused-arg warnings
    "$TMP_SDK/bin/veriobjc-cc" -O2 -Werror \
        -isystem "$REPO_ROOT/tests/plugin/isystem_ext" \
        -c sdk_dogfood.m -o sdk_dogfood.o
    "$TMP_SDK/bin/veriobjc-cc" -Werror \
        -isystem "$REPO_ROOT/tests/plugin/isystem_ext" \
        -E sdk_dogfood.m -o sdk_dogfood.i

    # Build-tree wrapper must also compile, link, and run cleanly
    "$BUILD_DIR/bin/veriobjc-cc" -O2 -Werror \
        -isystem "$REPO_ROOT/tests/plugin/isystem_ext" \
        sdk_dogfood.m -o sdk_dogfood_buildtree
    ./sdk_dogfood_buildtree "$TMP_WORK/frame.bin"
)

if nm "$TMP_WORK/sdk_dogfood" | grep -qE "objc_msgSend|objc_msg_lookup|__objc_exec_class|_OBJC_CLASS_|_OBJC_METACLASS_|[[:space:]]U[[:space:]]+malloc|[[:space:]]U[[:space:]]+free"; then
    echo "[FAIL] sdk_dogfood binary contains forbidden ObjC runtime or heap allocator symbols!"
    nm "$TMP_WORK/sdk_dogfood"
    exit 1
fi
echo "[PASS] Standalone VeriObjC executable built & verified via installed SDK ($TMP_SDK/bin/veriobjc-cc)"

# 4. Verify --smt-verify --smt-report flag forwarding under -Werror -fsyntax-only
cat > "$TMP_WORK/sdk_smt.m" <<'EOF'
#include <veriobjc_runtime.h>
#include <vstd/contracts.h>

vcontract_pre("val >= 0 && val <= 1000")
vcontract_post("result >= 10 && result <= 1010")
veriobjc_pure
int32_t add_fixed_offset(int32_t val) {
    return val + 10;
}
EOF

SMT_OUT="$(
    cd "$TMP_WORK"
    "$TMP_SDK/bin/veriobjc-cc" --smt-verify --smt-report --smt-timeout-ms=2000 \
        -Werror -fsyntax-only sdk_smt.m 2>&1
)"
if ! echo "$SMT_OUT" | grep -q "Function: add_fixed_offset | .*Status: UNSAT_PROVEN"; then
    echo "[FAIL] Expected --smt-verify --smt-report to prove add_fixed_offset UNSAT_PROVEN!"
    echo "$SMT_OUT"
    exit 1
fi
echo "[PASS] Z3 SMT flags (--smt-verify --smt-report) verified via veriobjc-cc"

# 5. Verify user-site diagnostics still fire when compiling against installed SDK headers
cat > "$TMP_WORK/sdk_bad_overflow.m" <<'EOF'
#include <veriobjc_runtime.h>
#include <vstd/vector.h>

VVector_decl(IntVec2, int32_t, 2);

void overflow_vec(void) {
    IntVec2 v = VVector_init(IntVec2);
    vvector_push(&v, 1);
    vvector_push(&v, 2);
    vvector_push(&v, 3);
}
EOF

if BAD_OUT="$("$TMP_SDK/bin/veriobjc-cc" -fsyntax-only "$TMP_WORK/sdk_bad_overflow.m" 2>&1)"; then
    echo "[FAIL] Expected user-site vvector overflow diagnostic under veriobjc-cc, but compilation succeeded!"
    exit 1
fi
if ! echo "$BAD_OUT" | grep -q "vvector push exceeds static capacity 2 on 'v'"; then
    echo "[FAIL] Missing expected user-site vvector overflow diagnostic under veriobjc-cc!"
    echo "$BAD_OUT"
    exit 1
fi
echo "[PASS] User-site container/macro diagnostics preserved with installed SDK headers"

# 6. Verify -fobjc-runtime=gnustep* is rejected cleanly with actionable error (no exit 70 crash)
set +e
GNUSTEP_OUT="$("$TMP_SDK/bin/veriobjc-cc" -fobjc-runtime=gnustep-2.0 -c "$TMP_WORK/sdk_dogfood.m" -o /dev/null 2>&1)"
GNUSTEP_RC=$?
set -e
if [ "$GNUSTEP_RC" -eq 0 ] || [ "$GNUSTEP_RC" -eq 70 ] || [ "$GNUSTEP_RC" -ge 128 ]; then
    echo "[FAIL] Expected clean non-crash rejection for -fobjc-runtime=gnustep-2.0, got rc=$GNUSTEP_RC"
    echo "$GNUSTEP_OUT"
    exit 1
fi
if ! echo "$GNUSTEP_OUT" | grep -q "veriobjc-cc: error:.*-fobjc-runtime=gnustep"; then
    echo "[FAIL] Expected actionable 'veriobjc-cc: error:' diagnostic for -fobjc-runtime=gnustep-2.0!"
    echo "$GNUSTEP_OUT"
    exit 1
fi
echo "[PASS] -fobjc-runtime=gnustep-2.0 rejected cleanly with actionable diagnostic (rc=$GNUSTEP_RC)"

echo "=== VeriObjC SDK & veriobjc-cc Gate Passed! ==="
