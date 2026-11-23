// Regression test (Task 6.0.2): live @selector(...) reference keeping .objc_sel_name*
// GlobalObject alive must never leave a dangling llvm::Comdat* in StaticDispatchLowering.
#include "veriobjc_runtime.h"
#include "rt/dispatch.h"

__attribute__((objc_root_class))
__attribute__((objc_sealed))
@interface ComdatBaseStage {
@public
    int32_t seed;
}
- (int32_t)evalStage;
@end

@implementation ComdatBaseStage
- (int32_t)evalStage {
    return seed;
}
@end

__attribute__((objc_sealed))
__attribute__((objc_final))
@interface ComdatLeafStage : ComdatBaseStage {
@public
    int32_t delta;
}
- (int32_t)evalStage;
@end

@implementation ComdatLeafStage
- (int32_t)evalStage {
    return seed + delta;
}
@end

int32_t run_comdat_selector_probe(struct veriobjc_stack *stack) {
    if (!stack) {
        return -1;
    }
    VERIOBJC_SCOPED_STACK(stack);
    ComdatLeafStage *leaf = veriobjc_alloc_sealed(ComdatLeafStage, stack);
    if (!leaf) {
        return -2;
    }
    leaf->seed = 30;
    leaf->delta = 12;

    SEL sel_handle = @selector(evalStage);
    ComdatBaseStage *base_ref = leaf;
    return (sel_handle != (SEL)0) ? [base_ref evalStage] : 0;
}

#ifdef VERIOBJC_COMDAT_E2E_MAIN
int main(void) {
    alignas(16) uint8_t buf[128];
    struct veriobjc_stack stack;
    veriobjc_stack_init(&stack, buf, sizeof(buf));
    if (run_comdat_selector_probe(&stack) != 42) {
        return 1;
    }
    return (veriobjc_stack_used(&stack) == 0u) ? 0 : 2;
}
#endif
