// Negative test: escaping an inner @scoped data-stack allocation laundered through an identity helper
#include "veriobjc_runtime.h"

typedef struct {
    int32_t sample;
} HelperItem;

static HelperItem *identity_passthrough(HelperItem *in_ptr) {
    return in_ptr;
}

void escape_via_identity_helper(struct veriobjc_stack *stack) {
    if (!stack) return;
    HelperItem *outer_ref = NULL;
    {
        VERIOBJC_SCOPED_STACK(stack);
        HelperItem *inner_ref = (HelperItem *)veriobjc_stack_alloc(stack, sizeof(HelperItem));
        outer_ref = identity_passthrough(inner_ref);
    }
    (void)outer_ref;
}
