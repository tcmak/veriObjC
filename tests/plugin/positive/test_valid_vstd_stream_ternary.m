#include "veriobjc_runtime.h"
#include "vstd/stream.h"
#include "vstd/ternary.h"

vbits_def(WeightCode2, 2);
vstream_def(SignalLanes4, int32_t, 4);
vstream_token_shift_def(ShiftBlock4, int32_t, 4);
vstream_kv_arena_def(KVCache8, int32_t, 2, 8);

int32_t run_valid_stream_ternary(void) {
    uint8_t w = vternary_pack4(VTERNARY_POS_ONE, VTERNARY_NEG_ONE, VTERNARY_ZERO, VTERNARY_POS_ONE);
    int32_t acts[4] = {8, 4, 2, 6};
    int32_t dot = vternary_dot_packed(acts, &w, 1u);
    int32_t gated = vternary_glu_mux_gate(dot, 1);
    int32_t normed = vternary_rmsnorm_shift(gated, 1u);

    KVCache8 kv;
    vstream_kv_arena_init(&kv);
    int32_t pair[2] = {normed, dot};
    vstream_kv_append(&kv, 0u, pair);
    return normed;
}
