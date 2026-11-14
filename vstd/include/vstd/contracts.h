#pragma once
#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <vstd/contracts.h> — Pure Contract Predicates & SMT Boundary Helpers
// Layer: vstd (depends on: rt)
//
// Invariants:
//   1. All contract helper predicates are @pure (0 surviving effects).
//   2. vcontract_pre / vcontract_post annotations emit SMT-ready boundary
//      comments in --export-header-stdout and feed Task 4.4's Z3 prover.
// ---------------------------------------------------------------------------

#define vcontract_pre(expr_str)  veriobjc_pre(expr_str)
#define vcontract_post(expr_str) veriobjc_post(expr_str)

__attribute__((annotate("objc_pure")))
static inline bool vcontract_in_range_i64(int64_t val, int64_t lo_incl, int64_t hi_incl) {
    return (val >= lo_incl) && (val <= hi_incl);
}

__attribute__((annotate("objc_pure")))
static inline bool vcontract_non_null(const void *ptr) {
    return ptr != NULL;
}

__attribute__((annotate("objc_pure")))
static inline bool vcontract_is_pow2_u64(uint64_t n) {
    return (n != 0ULL) && ((n & (n - 1ULL)) == 0ULL);
}

__attribute__((annotate("objc_pure")))
static inline bool vcontract_aligned_ptr(const void *ptr, size_t align_pow2) {
    if (!ptr) {
        return false;
    }
    return (((uintptr_t)ptr) & (align_pow2 - 1u)) == 0u;
}

__attribute__((annotate("objc_pure")))
static inline bool vcontract_implies(bool antecedent, bool consequent) {
    return (!antecedent) || consequent;
}

__attribute__((annotate("objc_pure")))
static inline bool vcontract_ensures_eq_i64(int64_t actual, int64_t expected) {
    return actual == expected;
}

#ifdef __cplusplus
}
#endif
