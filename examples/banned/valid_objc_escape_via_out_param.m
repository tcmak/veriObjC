// Valid stock Objective-C/C: writing a scoped arena pointer into an out-parameter.
// Rejected by VeriObjC (Task C2.2): clang::CFG provenance analysis rejects stack escape via out-parameter.
#include "veriobjc_runtime.h"

void valid_objc_escape_via_out_param(struct veriobjc_stack *st, uint32_t **out_slot) {
    if (!st || !out_slot) {
        return;
    }
    VERIOBJC_SCOPED_STACK(st);
    uint32_t *scratch = (uint32_t *)veriobjc_stack_alloc(st, sizeof(uint32_t));
    *out_slot = scratch;
}
