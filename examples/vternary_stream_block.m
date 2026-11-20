// examples/vternary_stream_block.m
// Demonstrates Task 3.9 (<vstd/stream.h>, <vstd/ternary.h>):
//   - Packed 2-bit ternary weights (4 per byte) verified via veriobjc_from_bytes
//   - Token shift register delay, multiplier-free ternary dot, GLU mux gate,
//     shift-only RMSNorm, and pre-banked KV cache arena with ContextCap
#include "veriobjc_runtime.h"
#include "vstd/stream.h"
#include "vstd/ternary.h"
#include "vstd/ttape_reader.h"

vstream_token_shift_def(TokenShiftReg4, int32_t, 4);
vstream_kv_arena_def(PreBankedKVArena8, int32_t, 2, 8);

int32_t run_ternary_stream_demo(void) {
    uint8_t raw_weights[4] = {
        vternary_pack4(VTERNARY_POS_ONE, VTERNARY_NEG_ONE, VTERNARY_ZERO, VTERNARY_POS_ONE),
        0u, 0u, 0u
    };
    uint8_t tape[64] __attribute__((aligned(8)));
    size_t tape_len = veriobjc_to_bytes(raw_weights, 1u, (uint32_t)sizeof(raw_weights), 0x5452u, tape, sizeof(tape));
    const uint8_t *verified_weights = NULL;
    if (!vternary_verify_weights_from_bytes(tape, tape_len, 0x5452u, (uint32_t)sizeof(raw_weights), &verified_weights)) {
        return -1;
    }

    TokenShiftReg4 shift_state;
    vstream_token_shift_init(&shift_state);
    int32_t curr_tok[4] = {10, 20, 30, 40};
    int32_t shifted[4] = {0, 0, 0, 0};
    vstream_token_shift_step(&shift_state, curr_tok, shifted);

    int32_t dot = vternary_dot_packed(shifted, verified_weights, 1u);
    int32_t gated = vternary_glu_mux_gate(dot, 1);
    int32_t normed = vternary_rmsnorm_shift(gated, 1u);

    PreBankedKVArena8 kv_arena;
    vstream_kv_arena_init(&kv_arena);
    int32_t kv_pair[2] = {normed, dot};
    vstream_kv_append(&kv_arena, 0u, kv_pair);
    const int32_t *stored = vstream_kv_at(&kv_arena, 0u);
    return stored ? stored[0] : -1;
}

#ifdef VERIOBJC_EXAMPLE_MAIN
int main(void) {
    return (run_ternary_stream_demo() == 20) ? 0 : 1;
}
#endif
