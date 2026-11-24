// Negative plugin test (Task 6.0.10):
// Raw pointer casts (or unchecked downcasts) to @sealed / @final Objective-C classes
// forge untagged objects without a preceding veriobjc_sealed_header, causing
// veriobjc_sealed_tag(obj) to read out-of-bounds memory behind the pointer.
#include "veriobjc_runtime.h"
#include "rt/dispatch.h"

__attribute__((objc_root_class))
__attribute__((objc_sealed))
@interface ForgedSealedNode {
@public
    int32_t weight;
}
- (int32_t)evalNode:(int32_t)x;
@end

@implementation ForgedSealedNode
- (int32_t)evalNode:(int32_t)x {
    return x + weight;
}
@end

__attribute__((objc_sealed))
__attribute__((objc_final))
@interface ForgedFinalLeaf : ForgedSealedNode {
@public
    int32_t bias;
}
- (int32_t)evalNode:(int32_t)x;
@end

@implementation ForgedFinalLeaf
- (int32_t)evalNode:(int32_t)x {
    return x + weight + bias;
}
@end

int32_t illegal_cast_raw_bytes_to_sealed(uint8_t *raw_bytes) {
    ForgedSealedNode *forged = (ForgedSealedNode *)raw_bytes;
    return forged ? [forged evalNode:1] : 0;
}

int32_t illegal_cast_stack_alloc_bypassing_sealed_alloc(struct veriobjc_stack *stack) {
    if (!stack) return 0;
    VERIOBJC_SCOPED_STACK(stack);
    ForgedFinalLeaf *untagged =
        (ForgedFinalLeaf *)veriobjc_stack_alloc(stack, sizeof(ForgedFinalLeaf));
    return untagged ? [untagged evalNode:2] : 0;
}

int32_t illegal_implicit_void_ptr_to_sealed(void *opaque_mem) {
    ForgedSealedNode *forged = opaque_mem;
    return forged ? [forged evalNode:3] : 0;
}

int32_t illegal_unchecked_downcast_to_final(ForgedSealedNode *base_ptr) {
    ForgedFinalLeaf *downcast = (ForgedFinalLeaf *)base_ptr;
    return downcast ? downcast->bias : 0;
}
