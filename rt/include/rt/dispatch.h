#ifndef VERIOBJC_RT_DISPATCH_H
#define VERIOBJC_RT_DISPATCH_H

#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <rt/dispatch.h> — Freestanding Closed-World Dispatch Primitives
// Layer: rt (zero vstd, sys0, hw, host, float, or libc dependencies)
// ---------------------------------------------------------------------------

typedef uint16_t veriobjc_type_tag_t;

static inline veriobjc_imp_t veriobjc_dispatch_by_sel_name(
    const struct veriobjc_object *obj,
    const char *sel_name
) {
    if (!obj || !sel_name) {
        return NULL;
    }
    return veriobjc_object_dispatch(obj, veriobjc_sel_hash(sel_name));
}

#ifdef __cplusplus
}
#endif

#endif /* VERIOBJC_RT_DISPATCH_H */
