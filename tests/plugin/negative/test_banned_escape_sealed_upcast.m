// Negative test: upcast @sealed base pointer escaping inner lexical VERIOBJC_SCOPED_STACK
#include "veriobjc_runtime.h"
#include "rt/dispatch.h"

__attribute__((objc_root_class))
__attribute__((objc_sealed))
@interface EscBaseNode {
@public
    int32_t tag_val;
}
- (int32_t)evalNode;
@end

@implementation EscBaseNode
- (int32_t)evalNode {
    return tag_val;
}
@end

__attribute__((objc_sealed))
__attribute__((objc_final))
@interface EscSubNode : EscBaseNode {
@public
    int64_t extra_payload;
}
- (int32_t)evalNode;
@end

@implementation EscSubNode
- (int32_t)evalNode {
    return tag_val + (int32_t)extra_payload;
}
@end

int32_t illegal_escape_upcast_sealed_ptr(struct veriobjc_stack *stack) {
    EscBaseNode *escaped_base = (EscBaseNode *)0;
    {
        VERIOBJC_SCOPED_STACK(stack);
        EscSubNode *sub = veriobjc_alloc_sealed(EscSubNode, stack);
        EscBaseNode *upcast = sub;
        escaped_base = upcast; // ERROR: inner stack-scoped sealed object escapes via upcast pointer
    }
    return escaped_base ? [escaped_base evalNode] : 0;
}
