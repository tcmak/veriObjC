// Negative test: storing a data-stack allocation into a variable that outlives its @scoped mark is banned
#include "veriobjc_runtime.h"

typedef struct {
    uint32_t id;
    float    value;
} ScratchItem;

void test_escaping_stack_reference(struct veriobjc_stack *stack) {
    ScratchItem *outer_escaped = NULL;

    {
        VERIOBJC_SCOPED_STACK(stack);
        ScratchItem *inner_item = (ScratchItem *)veriobjc_stack_alloc(stack, sizeof(ScratchItem));

        // Violation: inner_item is rooted in the inner VERIOBJC_SCOPED_STACK block,
        // which resets when this block exits, outlived by outer_escaped!
        outer_escaped = inner_item;
    }

    (void)outer_escaped;
}
