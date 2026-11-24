// Valid stock Objective-C/C: calling an external prototype with no body in the TU.
// Rejected by VeriObjC (Task 6.0.9): closed-world 0-DSP verification requires statically
// visible callee bodies and rejects opaque extern functions in objc_hw_kernel.
#include "veriobjc_runtime.h"

__attribute__((annotate("objc_pure"), annotate("objc_nolatency")))
extern int32_t opaque_external_scale_step(int32_t x, int32_t w);

__attribute__((annotate("objc_hw_kernel")))
int32_t valid_objc_hw_kernel_opaque_extern(int32_t x, int32_t w) {
    return opaque_external_scale_step(x, w);
}
