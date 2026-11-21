// Negative test (Task 5.5): escaping heterogeneous upcast @sealed pointers via compound-literal vspan
#include "veriobjc_runtime.h"
#include "rt/dispatch.h"
#include "vstd/span.h"

__attribute__((objc_root_class))
__attribute__((objc_sealed))
@interface CompSpanBase {
@public
    int32_t kind_code;
}
- (int32_t)score;
@end

@implementation CompSpanBase
- (int32_t)score { return kind_code; }
@end

__attribute__((objc_sealed))
__attribute__((objc_final))
@interface CompSpanWide : CompSpanBase {
@public
    int64_t wide_a;
    int64_t wide_b;
}
- (int32_t)score;
@end

@implementation CompSpanWide
- (int32_t)score { return kind_code + (int32_t)(wide_a + wide_b); }
@end

vspan_def(CompSpanSlice, CompSpanBase *);

int32_t illegal_escape_compound_literal_span_to_outer(struct veriobjc_stack *stack) {
    CompSpanSlice outer_slice = { .data = (CompSpanBase **)0, .len = 0u };
    {
        VERIOBJC_SCOPED_STACK(stack);
        CompSpanWide *w = veriobjc_alloc_sealed(CompSpanWide, stack);
        CompSpanBase **backing = (CompSpanBase **)veriobjc_stack_alloc(stack, sizeof(CompSpanBase *));
        if (w && backing) {
            backing[0] = w;
            // ERROR: compound-literal span carrying inner arena pointer assigned to outer_slice
            outer_slice = ((CompSpanSlice){ .data = backing, .len = 1u });
        }
    }
    CompSpanBase **elem_ptr = vspan_at(&outer_slice, 0u);
    return (elem_ptr && *elem_ptr) ? [*elem_ptr score] : 0;
}
