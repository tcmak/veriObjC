// Valid stock Objective-C: heterogeneous @sealed subclass pointers stored in a vspan
// escaping their lexical VERIOBJC_SCOPED_STACK arena via vspan_init into an outer slice.
// Rejected by VeriObjC because all upcast superclass pointers rooted in an inner arena
// become dangling after the single-store scope reset at block exit.
#include "veriobjc_runtime.h"
#include "rt/dispatch.h"
#include "vstd/span.h"

__attribute__((objc_root_class))
__attribute__((annotate("objc_sealed")))
@interface StageNode {
@public
    int32_t stage_id;
}
- (int32_t)runStage;
@end

@implementation StageNode
- (int32_t)runStage { return stage_id; }
@end

__attribute__((annotate("objc_sealed")))
__attribute__((annotate("objc_final")))
@interface CompactStage : StageNode {
@public
    int32_t bias;
}
- (int32_t)runStage;
@end

@implementation CompactStage
- (int32_t)runStage { return stage_id + bias; }
@end

__attribute__((annotate("objc_sealed")))
__attribute__((annotate("objc_final")))
@interface WideStage : StageNode {
@public
    int64_t lane0;
    int64_t lane1;
    int32_t gain;
}
- (int32_t)runStage;
@end

@implementation WideStage
- (int32_t)runStage { return stage_id + (int32_t)(lane0 + lane1) + gain; }
@end

vspan_def(StageNodeSpan, StageNode *);

int32_t leak_mixed_sealed_span_across_scope(struct veriobjc_stack *stack) {
    StageNodeSpan leaked_span = { .data = (StageNode **)0, .len = 0u };
    {
        VERIOBJC_SCOPED_STACK(stack);
        CompactStage *a = veriobjc_alloc_sealed(CompactStage, stack);
        WideStage *b = veriobjc_alloc_sealed(WideStage, stack);
        StageNode **slot_buf = (StageNode **)veriobjc_stack_alloc(stack, 2u * sizeof(StageNode *));
        if (a && b && slot_buf) {
            slot_buf[0] = a;
            slot_buf[1] = b;
            vspan_init(&leaked_span, slot_buf, 2u);
        }
    }
    StageNode **dangling = vspan_at(&leaked_span, 0u);
    return (dangling && *dangling) ? [*dangling runStage] : 0;
}
