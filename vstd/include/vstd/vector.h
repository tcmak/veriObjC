#pragma once
#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <vstd/vector.h> — Fixed-Capacity Inplace Containers (vvector<T, N>)
// Layer: vstd (depends on: rt)
//
// Invariants:
//   1. Elements live inline inside the struct footprint (zero heap allocation).
//   2. Pushing beyond Cap is rejected at compile time by VeriObjCPlugin and
//      returns false at runtime.
//   3. vvector_push_inplace constructs elements directly in the target slot.
// ---------------------------------------------------------------------------

#define vvector_def(VecName, ElemType, Cap) \
    typedef struct __attribute__((annotate("objc_vvector:" #Cap), annotate("objc_flat"))) { \
        uint32_t count; \
        uint32_t cap; \
        ElemType buf[Cap]; \
    } VecName

static inline void __vvector_init_raw(void *vec_obj, uint32_t *count_ptr, uint32_t *cap_ptr, uint32_t cap_val) {
    if (!vec_obj) {
        return;
    }
    *count_ptr = 0u;
    *cap_ptr = cap_val;
}

static inline bool __vvector_push_raw(
    void *vec_obj,
    uint32_t *count_ptr,
    uint32_t cap_val,
    void *buf_ptr,
    size_t elem_size,
    const void *val_ptr
) {
    if (!vec_obj || *count_ptr >= cap_val) {
        return false;
    }
    uint8_t *dst = (uint8_t *)buf_ptr + (size_t)(*count_ptr) * elem_size;
    const uint8_t *src = (const uint8_t *)val_ptr;
    for (size_t i = 0u; i < elem_size; ++i) {
        dst[i] = src[i];
    }
    (*count_ptr)++;
    return true;
}

static inline void *__vvector_alloc_slot_raw(
    void *vec_obj,
    uint32_t *count_ptr,
    uint32_t cap_val,
    void *buf_ptr,
    size_t elem_size
) {
    if (!vec_obj || *count_ptr >= cap_val) {
        return NULL;
    }
    void *slot = (void *)((uint8_t *)buf_ptr + (size_t)(*count_ptr) * elem_size);
    (*count_ptr)++;
    return slot;
}

static inline void *__vvector_at_raw(
    const void *vec_obj,
    uint32_t count_val,
    void *buf_ptr,
    size_t elem_size,
    uint32_t idx
) {
    if (!vec_obj || idx >= count_val) {
        return NULL;
    }
    return (void *)((uint8_t *)buf_ptr + (size_t)idx * elem_size);
}

static inline uint32_t __vvector_u32_or_zero(const void *vec_obj, const uint32_t *field_ptr) {
    if (!vec_obj) {
        return 0u;
    }
    return *field_ptr;
}

#ifdef __cplusplus
#define __vvector_val_ptr(ElemType, val) (&static_cast<const ElemType &>(val))
#else
#define __vvector_val_ptr(ElemType, val) ((const void *)(const ElemType[1]){ (val) })
#endif

#define vvector_init(vec_ptr) \
    __vvector_init_raw( \
        (vec_ptr), \
        (vec_ptr) ? &(vec_ptr)->count : (uint32_t *)NULL, \
        (vec_ptr) ? &(vec_ptr)->cap : (uint32_t *)NULL, \
        (uint32_t)(sizeof((vec_ptr)->buf) / sizeof((vec_ptr)->buf[0])))

#define vvector_push(vec_ptr, val) \
    __vvector_push_raw( \
        (vec_ptr), \
        (vec_ptr) ? &(vec_ptr)->count : (uint32_t *)NULL, \
        (vec_ptr) ? (vec_ptr)->cap : 0u, \
        (vec_ptr) ? (void *)(vec_ptr)->buf : NULL, \
        sizeof((vec_ptr)->buf[0]), \
        __vvector_val_ptr(__typeof__((vec_ptr)->buf[0]), (val)))

#define vvector_push_inplace(vec_ptr, builder_fn, arg_val) \
    ((__vvector_alloc_slot_raw( \
        (vec_ptr), \
        (vec_ptr) ? &(vec_ptr)->count : (uint32_t *)NULL, \
        (vec_ptr) ? (vec_ptr)->cap : 0u, \
        (vec_ptr) ? (void *)(vec_ptr)->buf : NULL, \
        sizeof((vec_ptr)->buf[0])) != NULL) \
        ? ((builder_fn)(&((vec_ptr)->buf[(vec_ptr)->count - 1u]), (arg_val)), true) \
        : false)

#define vvector_at(vec_ptr, idx) \
    ((__typeof__(&(vec_ptr)->buf[0]))__vvector_at_raw( \
        (vec_ptr), \
        (vec_ptr) ? (vec_ptr)->count : 0u, \
        (vec_ptr) ? (void *)(vec_ptr)->buf : NULL, \
        sizeof((vec_ptr)->buf[0]), \
        (uint32_t)(idx)))

#define vvector_len(vec_ptr) \
    __vvector_u32_or_zero((vec_ptr), (vec_ptr) ? &(vec_ptr)->count : (const uint32_t *)NULL)

#define vvector_cap(vec_ptr) \
    __vvector_u32_or_zero((vec_ptr), (vec_ptr) ? &(vec_ptr)->cap : (const uint32_t *)NULL)

#ifdef __cplusplus
}
#endif
