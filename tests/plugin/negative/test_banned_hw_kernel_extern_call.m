// Negative plugin test (Task 6.0.9b):
// Bodyless extern callees directly or transitively reachable from an objc_hw_kernel
// lack statically visible bodies for closed-world 0-DSP verification and MUST fail closed.
#include "veriobjc_runtime.h"

veriobjc_pure veriobjc_nolatency
extern int32_t external_opaque_dsp_mac(int32_t a, int32_t b);

veriobjc_pure veriobjc_nolatency
static int32_t helper_calling_extern_mac(int32_t a, int32_t b) {
    return external_opaque_dsp_mac(a, b);
}

veriobjc_hw_kernel
int32_t illegal_hw_kernel_direct_extern(int32_t x, int32_t y) {
    return external_opaque_dsp_mac(x, y);
}

veriobjc_hw_kernel
int32_t illegal_hw_kernel_transitive_extern(int32_t x, int32_t y) {
    return helper_calling_extern_mac(x, y);
}
