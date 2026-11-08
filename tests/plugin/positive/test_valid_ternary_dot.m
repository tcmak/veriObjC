// Positive test: 512-wide ternary dot product lowered to CMUX ladder + popcount reduction + log2(512)=9 reduce stages with 0 DSP blocks
#include "veriobjc_runtime.h"

vstream_def(ActivationStream512, int8_t, 512);
vstream_def(PackedTernaryWeights512, uint8_t, 128);

veriobjc_hw_kernel
int32_t evaluate_ternary_dot_512(
    const ActivationStream512 *act,
    const PackedTernaryWeights512 *weights
) {
    if (!act || !weights) {
        return 0;
    }
    uint32_t stages = veriobjc_clog2(512u);
    vstream_latency_contract(512u, stages);
    vstream_check_context(512u, 2048u);
    return veriobjc_ternary_dot_512(act->lanes, weights->lanes);
}
