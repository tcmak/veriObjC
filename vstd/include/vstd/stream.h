#pragma once
#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <vstd/stream.h> — Fixed-Width Wires, Token Shift & Pre-Banked KV Arenas
// Layer: vstd (depends on: rt)
//
// Invariants:
//   1. Bits<W> wires carry an explicit bitmask bounded to [1, 64] bits.
//   2. Token shift implements a 1-cycle register delay mixing the lower half
//      of the previous token with the upper half of the current token.
//   3. Pre-banked BRAM KV arenas carry a compile-time ContextCap enforced by
//      VeriObjCPlugin ("Context cap exceeded — compile error").
// ---------------------------------------------------------------------------

#define vbits_def(BitsName, Width) \
    typedef struct __attribute__((annotate("objc_vbits:" #Width), annotate("objc_flat"))) { \
        uint64_t bits; \
    } BitsName

static inline uint64_t __vbits_mask(uint32_t width, uint64_t raw_val) {
    uint64_t mask = (1ULL << width) - 1ULL;
    return raw_val & mask;
}

#define vbits_make(BitsName, Width, val) \
    ((BitsName){ .bits = __vbits_mask((uint32_t)(Width), (uint64_t)(val)) })

#ifndef vstream_def
#define vstream_def(StreamName, ElemType, Lanes) \
    typedef struct __attribute__((annotate("objc_vstream:" #Lanes), annotate("objc_flat"))) { \
        ElemType lanes[Lanes]; \
    } StreamName
#endif

#define vstream_token_shift_def(ShiftName, ElemType, Dim) \
    typedef struct __attribute__((annotate("objc_token_shift:" #Dim), annotate("objc_flat"))) { \
        ElemType prev_token[Dim]; \
    } ShiftName

static inline void __vstream_token_shift_init_raw(void *shift_obj, int32_t *prev_buf, uint32_t dim) {
    if (!shift_obj) {
        return;
    }
    for (uint32_t i = 0u; i < dim; ++i) {
        prev_buf[i] = 0;
    }
}

static inline void __vstream_token_shift_step_raw(
    void *shift_obj,
    int32_t *prev_buf,
    uint32_t dim,
    const int32_t *curr_token,
    int32_t *out_mixed
) {
    if (!shift_obj) {
        return;
    }
    uint32_t half = dim >> 1u;
    for (uint32_t i = 0u; i < dim; ++i) {
        out_mixed[i] = (i < half) ? prev_buf[i] : curr_token[i];
        prev_buf[i] = curr_token[i];
    }
}

#define vstream_token_shift_init(shift_ptr) \
    __vstream_token_shift_init_raw( \
        (shift_ptr), \
        (shift_ptr) ? (shift_ptr)->prev_token : (int32_t *)NULL, \
        (uint32_t)(sizeof((shift_ptr)->prev_token) / sizeof((shift_ptr)->prev_token[0])))

#define vstream_token_shift_step(shift_ptr, curr_arr, out_arr) \
    __vstream_token_shift_step_raw( \
        (shift_ptr), \
        (shift_ptr) ? (shift_ptr)->prev_token : (int32_t *)NULL, \
        (uint32_t)(sizeof((shift_ptr)->prev_token) / sizeof((shift_ptr)->prev_token[0])), \
        (curr_arr), \
        (out_arr))

#define vstream_kv_arena_def(ArenaName, ElemType, HeadDim, ContextCap) \
    typedef struct __attribute__((annotate("objc_kv_arena:" #ContextCap), annotate("objc_flat"))) { \
        uint32_t count; \
        uint32_t context_cap; \
        uint32_t head_dim; \
        ElemType slots[ContextCap][HeadDim]; \
    } ArenaName

static inline void __vstream_kv_arena_init_raw(
    void *arena_obj,
    uint32_t *count_ptr,
    uint32_t *cap_ptr,
    uint32_t *dim_ptr,
    uint32_t cap_val,
    uint32_t dim_val
) {
    if (!arena_obj) {
        return;
    }
    *count_ptr = 0u;
    *cap_ptr = cap_val;
    *dim_ptr = dim_val;
}

static inline bool __vstream_kv_append_raw(
    void *arena_obj,
    uint32_t *count_ptr,
    uint32_t cap_val,
    uint32_t dim_val,
    int32_t *slots_ptr,
    uint32_t token_idx,
    const int32_t *kv_pair
) {
    if (!arena_obj || !kv_pair || token_idx >= cap_val) {
        return false;
    }
    int32_t *dst = slots_ptr + (size_t)token_idx * (size_t)dim_val;
    for (uint32_t i = 0u; i < dim_val; ++i) {
        dst[i] = kv_pair[i];
    }
    *count_ptr = token_idx + 1u;
    return true;
}

static inline const int32_t *__vstream_kv_at_raw(
    const void *arena_obj,
    uint32_t count_val,
    uint32_t dim_val,
    const int32_t *slots_ptr,
    uint32_t token_idx
) {
    if (!arena_obj || token_idx >= count_val) {
        return NULL;
    }
    return slots_ptr + (size_t)token_idx * (size_t)dim_val;
}

#define vstream_kv_arena_init(arena_ptr) \
    __vstream_kv_arena_init_raw( \
        (arena_ptr), \
        (arena_ptr) ? &(arena_ptr)->count : (uint32_t *)NULL, \
        (arena_ptr) ? &(arena_ptr)->context_cap : (uint32_t *)NULL, \
        (arena_ptr) ? &(arena_ptr)->head_dim : (uint32_t *)NULL, \
        (uint32_t)(sizeof((arena_ptr)->slots) / sizeof((arena_ptr)->slots[0])), \
        (uint32_t)(sizeof((arena_ptr)->slots[0]) / sizeof((arena_ptr)->slots[0][0])))

#define vstream_kv_append(arena_ptr, token_idx, kv_ptr) \
    __vstream_kv_append_raw( \
        (arena_ptr), \
        (arena_ptr) ? &(arena_ptr)->count : (uint32_t *)NULL, \
        (arena_ptr) ? (arena_ptr)->context_cap : 0u, \
        (arena_ptr) ? (arena_ptr)->head_dim : 0u, \
        (arena_ptr) ? &(arena_ptr)->slots[0][0] : (int32_t *)NULL, \
        (uint32_t)(token_idx), \
        (kv_ptr))

#define vstream_kv_at(arena_ptr, token_idx) \
    __vstream_kv_at_raw( \
        (arena_ptr), \
        (arena_ptr) ? (arena_ptr)->count : 0u, \
        (arena_ptr) ? (arena_ptr)->head_dim : 0u, \
        (arena_ptr) ? &(arena_ptr)->slots[0][0] : (const int32_t *)NULL, \
        (uint32_t)(token_idx))

#ifdef __cplusplus
}
#endif
