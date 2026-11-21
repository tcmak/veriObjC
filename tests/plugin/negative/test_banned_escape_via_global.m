// Negative test: escaping a scoped data-stack allocation into a global variable
#include "veriobjc_runtime.h"

typedef struct {
    uint32_t tag;
    int32_t  payload;
} GlobalEscapeItem;

static GlobalEscapeItem *g_escaped_slot = NULL;

void escape_stack_ref_to_global(struct veriobjc_stack *stack) {
    if (!stack) return;
    VERIOBJC_SCOPED_STACK(stack);
    GlobalEscapeItem *local_item = (GlobalEscapeItem *)veriobjc_stack_alloc(stack, sizeof(GlobalEscapeItem));
    g_escaped_slot = local_item;
}
