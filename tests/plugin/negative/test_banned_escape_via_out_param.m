// Negative test: escaping a scoped data-stack allocation via an out-parameter pointer
#include "veriobjc_runtime.h"

typedef struct {
    uint64_t seq_no;
} OutParamFrame;

void escape_stack_ref_via_out_param(struct veriobjc_stack *stack, OutParamFrame **out_frame) {
    if (!stack || !out_frame) return;
    VERIOBJC_SCOPED_STACK(stack);
    OutParamFrame *inner_frame = (OutParamFrame *)veriobjc_stack_alloc(stack, sizeof(OutParamFrame));
    *out_frame = inner_frame;
}
