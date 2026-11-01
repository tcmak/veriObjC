#include "veriobjc_runtime.h"

// Explicit exported wrapper to examine emitted freestanding machine code
void veriobjc_runtime_scope_exit_test(struct veriobjc_stack *s, uint8_t *mark) {
    veriobjc_stack_reset(s, mark);
}

void* veriobjc_runtime_alloc_test(struct veriobjc_stack *s, size_t n) {
    return veriobjc_stack_alloc(s, n);
}

struct veriobjc_handle veriobjc_runtime_gen_alloc_test(struct veriobjc_gen_pool *p) {
    return veriobjc_gen_pool_alloc(p);
}

void* veriobjc_runtime_gen_resolve_test(struct veriobjc_gen_pool *p, struct veriobjc_handle h) {
    return veriobjc_gen_pool_resolve(p, h);
}

