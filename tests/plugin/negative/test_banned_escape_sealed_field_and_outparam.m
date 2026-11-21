// Negative test (Task 5.5): storing upcast @sealed superclass pointer from heterogeneous batch
// into an outer-scope struct field and into an out-parameter (*out_node).
#include "veriobjc_runtime.h"
#include "rt/dispatch.h"

__attribute__((objc_root_class))
__attribute__((objc_sealed))
@interface SinkBaseNode {
@public
    int32_t base_id;
}
- (int32_t)evalMetric;
@end

@implementation SinkBaseNode
- (int32_t)evalMetric { return base_id; }
@end

__attribute__((objc_sealed))
__attribute__((objc_final))
@interface SinkSmallNode : SinkBaseNode {
@public
    int32_t delta;
}
- (int32_t)evalMetric;
@end

@implementation SinkSmallNode
- (int32_t)evalMetric { return base_id + delta; }
@end

__attribute__((objc_sealed))
__attribute__((objc_final))
@interface SinkLargeNode : SinkBaseNode {
@public
    int64_t accum_lo;
    int64_t accum_hi;
    int32_t scale;
}
- (int32_t)evalMetric;
@end

@implementation SinkLargeNode
- (int32_t)evalMetric { return base_id + (int32_t)(accum_lo + accum_hi) + scale; }
@end

struct OuterNodeHolder {
    SinkBaseNode *saved;
};

int32_t illegal_escape_sealed_to_field_and_out_param(
    struct veriobjc_stack *stack,
    SinkBaseNode **out_node
) {
    struct OuterNodeHolder holder = { .saved = (SinkBaseNode *)0 };
    {
        VERIOBJC_SCOPED_STACK(stack);
        SinkSmallNode *s = veriobjc_alloc_sealed(SinkSmallNode, stack);
        SinkLargeNode *l = veriobjc_alloc_sealed(SinkLargeNode, stack);
        SinkBaseNode *batch[2] = { s, l };
        holder.saved = batch[0]; // ERROR: escapes to outer struct field 'holder.saved'
        if (out_node) {
            *out_node = batch[1]; // ERROR: escapes to out-parameter '*out_node'
        }
    }
    return holder.saved ? [holder.saved evalMetric] : 0;
}
