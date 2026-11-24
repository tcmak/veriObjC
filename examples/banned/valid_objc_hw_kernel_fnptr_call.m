// Valid stock Objective-C/C: invoking a function pointer inside an annotated kernel.
// Rejected by VeriObjC (Task 6.0.9): closed-world 0-DSP verification requires statically
// visible direct callees and forbids indirect function-pointer calls in objc_hw_kernel.
#include "veriobjc_runtime.h"

typedef int32_t (*kernel_step_fn_t)(int32_t, int32_t);

__attribute__((annotate("objc_hw_kernel")))
int32_t valid_objc_hw_kernel_indirect_fnptr(kernel_step_fn_t step, int32_t a, int32_t b) {
    return step(a, b);
}
