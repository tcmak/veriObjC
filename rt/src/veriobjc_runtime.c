#include "veriobjc_runtime.h"
#include "rt/dispatch.h"

// Minimal freestanding anchor symbol so libveriobjc_rt.a is never an empty archive.
const uint32_t veriobjc_rt_abi_version = 1u;

#ifdef VERIOBJC_ASM_PROBE
// Explicit opt-in disassembly-inspection wrappers (compile with -DVERIOBJC_ASM_PROBE)
void veriobjc_runtime_scope_exit_test(struct veriobjc_stack *s, uint8_t *mark) {
    veriobjc_stack_reset(s, mark);
}

void *veriobjc_runtime_alloc_test(struct veriobjc_stack *s, size_t n) {
    return veriobjc_stack_alloc(s, n);
}

struct veriobjc_handle veriobjc_runtime_gen_alloc_test(struct veriobjc_gen_pool *p) {
    return veriobjc_gen_pool_alloc(p);
}

void *veriobjc_runtime_gen_resolve_test(struct veriobjc_gen_pool *p, struct veriobjc_handle h) {
    return veriobjc_gen_pool_resolve(p, h);
}

void veriobjc_runtime_scope_guard_test(struct veriobjc_scope_guard *g) {
    veriobjc_scope_cleanup(g);
}

veriobjc_imp_t veriobjc_runtime_dispatch_test(const struct veriobjc_class *cls, uint32_t sel_id) {
    return veriobjc_dispatch_lookup(cls, sel_id);
}

void *veriobjc_runtime_rel32_get_test(const struct veriobjc_rel32 *link) {
    return veriobjc_rel32_get(link);
}
#endif /* VERIOBJC_ASM_PROBE */
