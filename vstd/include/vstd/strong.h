#pragma once
#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <vstd/strong.h> — Zero-Cost Nominal Newtype Wrappers (vstrong)
// Layer: vstd (depends on: rt)
// ---------------------------------------------------------------------------

#define vstrong_make(StrongType, val) \
    ((StrongType){ .raw = (val) })

#define vstrong_unwrap(strong_val) \
    ((strong_val).raw)

static inline bool __vstrong_cmp_id(bool cmp_val) {
    return cmp_val;
}

#define vstrong_eq(a, b) \
    ((void)(&(a) == &(b)), __vstrong_cmp_id((a).raw == (b).raw))

#define vstrong_lt(a, b) \
    ((void)(&(a) == &(b)), __vstrong_cmp_id((a).raw < (b).raw))

#ifdef __cplusplus
}
#endif
