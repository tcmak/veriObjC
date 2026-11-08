#include "veriobjc_runtime.h"

#define TERNARY_WEIGHT_TAG 0x5457u // "TW"

vstream_def(ActivationStream512, int8_t, 512);
vstream_def(PackedTernaryWeights512, uint8_t, 128);

__attribute__((objc_root_class))
__attribute__((objc_sealed))
__attribute__((objc_final))
@interface TernaryGLUBlock {
@public
    uint32_t layer_id;
    uint32_t use_prev_token;
}
- (int32_t)forward:(const ActivationStream512 *)x
              prev:(const ActivationStream512 *)prev_x
          gateTape:(const void *)gate_tape_buf
           tapeLen:(size_t)gate_tape_len
            upTape:(const void *)up_tape_buf
         upTapeLen:(size_t)up_tape_len veriobjc_hw_kernel;
@end

@implementation TernaryGLUBlock
- (int32_t)forward:(const ActivationStream512 *)x
              prev:(const ActivationStream512 *)prev_x
          gateTape:(const void *)gate_tape_buf
           tapeLen:(size_t)gate_tape_len
            upTape:(const void *)up_tape_buf
         upTapeLen:(size_t)up_tape_len {
    if (!x || !prev_x || !gate_tape_buf || !up_tape_buf) {
        return 0;
    }

    // Verify log2(512) = 9 reduce stages & context cap at compile time
    uint32_t reduce_stages = veriobjc_clog2(512u);
    vstream_latency_contract(512u, reduce_stages);
    vstream_check_context(512u, 2048u);

    // Weight provenance check: verify content-addressed packed ternary weight snapshots via from_bytes
    uint32_t gate_count = 0u;
    const PackedTernaryWeights512 *w_gate = (const PackedTernaryWeights512 *)veriobjc_from_bytes(
        gate_tape_buf, gate_tape_len, TERNARY_WEIGHT_TAG, sizeof(PackedTernaryWeights512), &gate_count
    );
    uint32_t up_count = 0u;
    const PackedTernaryWeights512 *w_up = (const PackedTernaryWeights512 *)veriobjc_from_bytes(
        up_tape_buf, up_tape_len, TERNARY_WEIGHT_TAG, sizeof(PackedTernaryWeights512), &up_count
    );
    if (!w_gate || !w_up || gate_count == 0u || up_count == 0u) {
        return 0;
    }

    // Token shift via register-delayed stream selection
    const int8_t *shifted_lanes = (use_prev_token != 0u) ? prev_x->lanes : x->lanes;

    // 512-wide CMUX + popcount adder-tree dot products (0 multipliers, 0 DSPs)
    int32_t g = veriobjc_ternary_dot_512(shifted_lanes, w_gate->lanes);
    int32_t u = veriobjc_ternary_dot_512(x->lanes, w_up->lanes);

    // Sign-mux GLU gate (0 DSPs)
    return veriobjc_glu_gate(g, u);
}
@end

int32_t run_ternary_glu_inference(
    TernaryGLUBlock *block,
    const ActivationStream512 *x,
    const ActivationStream512 *prev_x,
    const void *gate_tape,
    size_t gate_len,
    const void *up_tape,
    size_t up_len
) {
    if (!block) {
        return 0;
    }
    return [block forward:x prev:prev_x gateTape:gate_tape tapeLen:gate_len upTape:up_tape upTapeLen:up_len];
}
