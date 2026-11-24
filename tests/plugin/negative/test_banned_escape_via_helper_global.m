// Negative plugin test (Task 6.0.8a):
// Transitive helper-sink escape: passing a StackScoped / local address into a helper
// chain that stashes the pointer into a global/static variable or out-parameter.
#include "veriobjc_runtime.h"

static int32_t *g_escaped_cursor = NULL;

static void leaf_stash_global(int32_t *p) {
    g_escaped_cursor = p;
}

static void mid_relay_to_global(int32_t *p) {
    leaf_stash_global(p);
}

void illegal_escape_via_helper_global_chain(struct veriobjc_stack *stack) {
    if (!stack) return;
    VERIOBJC_SCOPED_STACK(stack);
    int32_t *scratch = (int32_t *)veriobjc_stack_alloc(stack, sizeof(int32_t));
    if (!scratch) return;
    *scratch = 7;
    mid_relay_to_global(scratch);
}

static void helper_write_out_param(int32_t **out_slot, int32_t *candidate) {
    if (out_slot) {
        *out_slot = candidate;
    }
}

void illegal_escape_via_helper_out_param(struct veriobjc_stack *stack, int32_t **caller_out) {
    if (!stack) return;
    int32_t *outer_box = NULL;
    {
        VERIOBJC_SCOPED_STACK(stack);
        int32_t *inner_slot = (int32_t *)veriobjc_stack_alloc(stack, sizeof(int32_t));
        helper_write_out_param(&outer_box, inner_slot);
        helper_write_out_param(caller_out, inner_slot);
    }
    (void)outer_box;
}
