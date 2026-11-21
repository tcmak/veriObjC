// Negative test: escaping a subobject address (&inner[0]) from an inner @scoped stack allocation
#include "veriobjc_runtime.h"

void escape_subobject_address(struct veriobjc_stack *stack) {
    if (!stack) return;
    int32_t *outer_lane = NULL;
    {
        VERIOBJC_SCOPED_STACK(stack);
        int32_t *inner = (int32_t *)veriobjc_stack_alloc(stack, 4u * sizeof(int32_t));
        outer_lane = &inner[0];
    }
    (void)outer_lane;
}
