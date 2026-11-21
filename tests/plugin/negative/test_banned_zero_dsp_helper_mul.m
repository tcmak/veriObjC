// Negative test: transitive runtime multiplication/division hidden inside a helper called by an objc_hw_kernel
#include "veriobjc_runtime.h"

static int32_t inner_dsp_scale(int32_t x, int32_t weight) {
    return x * weight;
}

static int32_t outer_helper_step(int32_t x, int32_t weight) {
    return inner_dsp_scale(x, weight);
}

veriobjc_hw_kernel
int32_t illegal_zero_dsp_kernel_calling_helper(int32_t act, int32_t w) {
    return outer_helper_step(act, w);
}
