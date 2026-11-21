// End-to-end freestanding @sealed object model test (Task C2.3):
//   - @sealed root class + mixed-size @final subclasses
//   - Scoped arena instantiation via veriobjc_alloc_sealed (zero heap / zero -lobjc)
//   - Direct send [sub compute] == 42 AND polymorphic send via Base *p = sub; [p compute] == 42
//   - Inherited vs overridden selector resolution across mixed-size subclasses (zero slicing)
//   - Lexical VERIOBJC_SCOPED_STACK single-store reset to 0 used bytes on scope exit
#include "veriobjc_runtime.h"
#include "rt/dispatch.h"

__attribute__((objc_root_class))
__attribute__((objc_sealed))
@interface SealedComputeNode {
@public
    int32_t base_bias;
}
- (int32_t)compute;
- (int32_t)scaleBy:(int32_t)factor;
@end

@implementation SealedComputeNode
- (int32_t)compute {
    return 1;
}
- (int32_t)scaleBy:(int32_t)factor {
    return base_bias + factor;
}
@end

__attribute__((objc_sealed))
__attribute__((objc_final))
@interface LinearComputeNode : SealedComputeNode {
@public
    int32_t slope;
    int32_t input_x;
}
- (int32_t)compute;
- (int32_t)scaleBy:(int32_t)factor;
@end

@implementation LinearComputeNode
- (int32_t)compute {
    return base_bias + slope * input_x;
}
- (int32_t)scaleBy:(int32_t)factor {
    return (base_bias + slope * input_x) * factor;
}
@end

__attribute__((objc_sealed))
__attribute__((objc_final))
@interface AffineComputeNode : SealedComputeNode {
@public
    int64_t wide_acc;
    int32_t delta;
    int32_t shift_amt;
}
- (int32_t)compute;
@end

@implementation AffineComputeNode
- (int32_t)compute {
    return (int32_t)(wide_acc >> shift_amt) + base_bias + delta;
}
@end

__attribute__((noinline))
static int32_t eval_polymorphic_compute(SealedComputeNode *node) {
    if (!node) {
        return -1;
    }
    return [node compute];
}

__attribute__((noinline))
static int32_t eval_polymorphic_scale(SealedComputeNode *node, int32_t factor) {
    if (!node) {
        return -1;
    }
    return [node scaleBy:factor];
}

int main(void) {
    alignas(16) uint8_t arena_buf[256];
    struct veriobjc_stack stack;
    veriobjc_stack_init(&stack, arena_buf, sizeof(arena_buf));

    if (veriobjc_stack_used(&stack) != 0u) {
        return 10;
    }

    {
        VERIOBJC_SCOPED_STACK(&stack);

        LinearComputeNode *sub = veriobjc_alloc_sealed(LinearComputeNode, &stack);
        AffineComputeNode *aff = veriobjc_alloc_sealed(AffineComputeNode, &stack);
        if (!sub || !aff) {
            return 1;
        }

        // 2 + 5 * 8 == 42
        sub->base_bias = 2;
        sub->slope = 5;
        sub->input_x = 8;

        // (128 >> 1) + 10 + 25 == 99 (verifies 24-byte subclass ivars are intact, zero slicing)
        aff->base_bias = 10;
        aff->wide_acc = 128;
        aff->delta = 25;
        aff->shift_amt = 1;

        if (veriobjc_stack_used(&stack) == 0u) {
            return 2;
        }

        // 1. Direct message send on @final subclass pointer
        if ([sub compute] != 42) {
            return 3;
        }

        // 2. Polymorphic message send through @sealed base-typed pointer (must be 42, NOT base 1)
        SealedComputeNode *p = sub;
        if ([p compute] != 42) {
            return 4;
        }
        if (eval_polymorphic_compute(p) != 42) {
            return 5;
        }

        // 3. Polymorphic message send on mixed-size sibling subclass through base pointer
        SealedComputeNode *q = aff;
        if (eval_polymorphic_compute(q) != 99) {
            return 6;
        }

        // 4. Overridden vs superclass-inherited selector resolution through base pointer
        if (eval_polymorphic_scale(p, 2) != 84) {
            return 7;
        }
        if (eval_polymorphic_scale(q, 3) != 13) {
            return 8;
        }

        // 5. Base instance itself resolves default branch (compute == 1)
        SealedComputeNode *base_inst = veriobjc_alloc_sealed(SealedComputeNode, &stack);
        if (!base_inst) {
            return 9;
        }
        base_inst->base_bias = 7;
        if (eval_polymorphic_compute(base_inst) != 1) {
            return 11;
        }
    }

    // 6. Scope exit must reset heterogeneous arena to 0 used bytes in one store
    if (veriobjc_stack_used(&stack) != 0u) {
        return 12;
    }

    return 0;
}
