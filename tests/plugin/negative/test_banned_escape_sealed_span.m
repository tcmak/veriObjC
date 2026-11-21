// Negative test (Task 5.5): escaping heterogeneous upcast @sealed pointers via vspan_init into outer slice
#include "veriobjc_runtime.h"
#include "rt/dispatch.h"
#include "vstd/span.h"

__attribute__((objc_root_class))
__attribute__((objc_sealed))
@interface SpanEscBase {
@public
    int32_t kind_code;
}
- (int32_t)score;
@end

@implementation SpanEscBase
- (int32_t)score { return kind_code; }
@end

__attribute__((objc_sealed))
__attribute__((objc_final))
@interface SpanEscCompact : SpanEscBase {
@public
    uint16_t flags;
}
- (int32_t)score;
@end

@implementation SpanEscCompact
- (int32_t)score { return kind_code + (int32_t)flags; }
@end

__attribute__((objc_sealed))
__attribute__((objc_final))
@interface SpanEscWide : SpanEscBase {
@public
    int64_t wide_a;
    int64_t wide_b;
    int32_t extra_tag;
}
- (int32_t)score;
@end

@implementation SpanEscWide
- (int32_t)score { return kind_code + (int32_t)(wide_a + wide_b) + extra_tag; }
@end

vspan_def(SpanEscSlice, SpanEscBase *);

int32_t illegal_escape_upcast_via_vspan_init(struct veriobjc_stack *stack) {
    SpanEscSlice outer_slice = { .data = (SpanEscBase **)0, .len = 0u };
    {
        VERIOBJC_SCOPED_STACK(stack);
        SpanEscCompact *c = veriobjc_alloc_sealed(SpanEscCompact, stack);
        SpanEscWide *w = veriobjc_alloc_sealed(SpanEscWide, stack);
        SpanEscBase **backing = (SpanEscBase **)veriobjc_stack_alloc(stack, 2u * sizeof(SpanEscBase *));
        if (c && w && backing) {
            backing[0] = c;
            backing[1] = w;
            // ERROR: escapes inner stack-scoped upcast array into outer_slice via vspan_init
            vspan_init(&outer_slice, backing, 2u);
        }
    }
    SpanEscBase **slot = vspan_at(&outer_slice, 0u);
    return (slot && *slot) ? [*slot score] : 0;
}
