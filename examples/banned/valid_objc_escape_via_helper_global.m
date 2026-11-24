// Valid stock Objective-C/C: helper stashes a caller's pointer into static storage.
// Rejected by VeriObjC (Task 6.0.8): StackScoped references cannot escape to globals
// through helper call chains.
#include "veriobjc_runtime.h"

static uint32_t *g_last_seen_slot = NULL;

static void record_slot_globally(uint32_t *slot) {
    g_last_seen_slot = slot;
}

void valid_objc_escape_via_helper_global(struct veriobjc_stack *stack) {
    if (!stack) return;
    VERIOBJC_SCOPED_STACK(stack);
    uint32_t *tmp = (uint32_t *)veriobjc_stack_alloc(stack, sizeof(uint32_t));
    if (tmp) {
        *tmp = 99u;
        record_slot_globally(tmp);
    }
}
