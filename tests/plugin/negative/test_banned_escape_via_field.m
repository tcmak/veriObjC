// Negative test: escaping an inner-scope data-stack allocation via an outer struct field
#include "veriobjc_runtime.h"

typedef struct {
    int32_t value;
} FieldPayload;

typedef struct {
    FieldPayload *saved;
} OuterHolder;

void escape_stack_ref_via_field(struct veriobjc_stack *stack) {
    if (!stack) return;
    OuterHolder holder = { .saved = NULL };
    {
        VERIOBJC_SCOPED_STACK(stack);
        FieldPayload *inner_p = (FieldPayload *)veriobjc_stack_alloc(stack, sizeof(FieldPayload));
        holder.saved = inner_p;
    }
    (void)holder;
}
