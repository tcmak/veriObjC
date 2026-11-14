#pragma once
#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <vstd/simd.h> — Hardware-Aware Portable SIMD Vectorization
// Layer: vstd (depends on: rt)
//
// Invariants:
//   1. Raw vendor intrinsics (_mm_*, __builtin_ia32_*, vaddq_*) are banned
//      in @nolatency user code; all vector ops route through <vstd/simd.h>.
//   2. Operations match the scalar reference byte-for-byte across targets.
// ---------------------------------------------------------------------------

typedef struct __attribute__((aligned(16), annotate("objc_flat"))) {
    int32_t lanes[4];
} vsimd_i32x4;

static inline vsimd_i32x4 vsimd_load_i32x4(const int32_t *src4) {
    if (!src4) {
        vsimd_i32x4 z = { .lanes = {0, 0, 0, 0} };
        return z;
    }
    vsimd_i32x4 v = { .lanes = {src4[0], src4[1], src4[2], src4[3]} };
    return v;
}

static inline void vsimd_store_i32x4(int32_t *dst4, vsimd_i32x4 v) {
    if (!dst4) {
        return;
    }
    for (int i = 0; i < 4; ++i) {
        dst4[i] = v.lanes[i];
    }
}

static inline vsimd_i32x4 vsimd_add_i32x4(vsimd_i32x4 a, vsimd_i32x4 b) {
    vsimd_i32x4 r;
    for (int i = 0; i < 4; ++i) {
        r.lanes[i] = a.lanes[i] + b.lanes[i];
    }
    return r;
}

static inline vsimd_i32x4 vsimd_mul_i32x4(vsimd_i32x4 a, vsimd_i32x4 b) {
    vsimd_i32x4 r;
    for (int i = 0; i < 4; ++i) {
        r.lanes[i] = a.lanes[i] * b.lanes[i];
    }
    return r;
}

static inline vsimd_i32x4 vsimd_select_gt_i32x4(
    vsimd_i32x4 cmp_lhs,
    vsimd_i32x4 cmp_rhs,
    vsimd_i32x4 on_true,
    vsimd_i32x4 on_false
) {
    vsimd_i32x4 r;
    for (int i = 0; i < 4; ++i) {
        r.lanes[i] = (cmp_lhs.lanes[i] > cmp_rhs.lanes[i]) ? on_true.lanes[i] : on_false.lanes[i];
    }
    return r;
}

static inline int32_t vsimd_reduce_add_i32x4(vsimd_i32x4 v) {
    return (v.lanes[0] + v.lanes[1]) + (v.lanes[2] + v.lanes[3]);
}

#ifdef __cplusplus
}
#endif
