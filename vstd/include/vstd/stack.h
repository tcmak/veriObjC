#pragma once
#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <vstd/stack.h> — Grow-Only Data-Stack Arrays (vstack_array<T>)
// Layer: vstd (depends on: rt)
//
// Invariants:
//   1. At most ONE growable vstack_array is live on top of a data stack.
//   2. Growth advances stack->top and NEVER moves existing elements.
//   3. Scope exit resets stack->top in a single store (VERIOBJC_SCOPED_STACK).
// ---------------------------------------------------------------------------

#define vstack_array_def(ArrayName, ElemType) \
    typedef struct { \
        struct veriobjc_stack *backing; \
        ElemType              *base_elems; \
        uint32_t               count; \
    } ArrayName

static inline void __vstack_array_init_raw(
    void *arr_obj,
    struct veriobjc_stack **out_backing,
    void **out_base_elems,
    uint32_t *out_count,
    struct veriobjc_stack *s
) {
    if (!arr_obj) {
        return;
    }
    *out_backing = s;
    *out_base_elems = s ? (void *)s->top : NULL;
    *out_count = 0u;
}

static inline void *__vstack_array_push_raw(
    void *arr_obj,
    struct veriobjc_stack *s,
    uint32_t *count_ptr,
    size_t elem_size,
    const void *val_ptr
) {
    if (!arr_obj) {
        return NULL;
    }
    uint8_t *slot = (uint8_t *)veriobjc_stack_alloc(s, elem_size);
    if (!slot) {
        return NULL;
    }
    const uint8_t *src = (const uint8_t *)val_ptr;
    for (size_t i = 0u; i < elem_size; ++i) {
        slot[i] = src[i];
    }
    (*count_ptr)++;
    return slot;
}

static inline void *__vstack_array_at_raw(
    const void *arr_obj,
    void *base_elems,
    uint32_t count,
    size_t elem_size,
    uint32_t idx
) {
    if (!arr_obj || idx >= count) {
        return NULL;
    }
    size_t stride = veriobjc_align_up(elem_size, VERIOBJC_DEFAULT_ALIGN);
    return (void *)((uint8_t *)base_elems + (size_t)idx * stride);
}

#ifdef __cplusplus
#define __vstack_val_ptr(ElemType, val) (&static_cast<const ElemType &>(val))
#else
#define __vstack_val_ptr(ElemType, val) ((const void *)(const ElemType[1]){ (val) })
#endif

#define vstack_array_init(arr_ptr, stack_ptr) \
    __vstack_array_init_raw( \
        (arr_ptr), \
        (arr_ptr) ? &(arr_ptr)->backing : (struct veriobjc_stack **)NULL, \
        (arr_ptr) ? (void **)&(arr_ptr)->base_elems : (void **)NULL, \
        (arr_ptr) ? &(arr_ptr)->count : (uint32_t *)NULL, \
        (stack_ptr))

#define vstack_array_push(arr_ptr, val) \
    ((__typeof__((arr_ptr)->base_elems))__vstack_array_push_raw( \
        (arr_ptr), \
        (arr_ptr) ? (arr_ptr)->backing : (struct veriobjc_stack *)NULL, \
        (arr_ptr) ? &(arr_ptr)->count : (uint32_t *)NULL, \
        sizeof(*(arr_ptr)->base_elems), \
        __vstack_val_ptr(__typeof__(*(arr_ptr)->base_elems), (val))))

#define vstack_array_at(arr_ptr, idx) \
    ((__typeof__((arr_ptr)->base_elems))__vstack_array_at_raw( \
        (arr_ptr), \
        (arr_ptr) ? (void *)(arr_ptr)->base_elems : NULL, \
        (arr_ptr) ? (arr_ptr)->count : 0u, \
        sizeof(*(arr_ptr)->base_elems), \
        (uint32_t)(idx)))

#ifdef __cplusplus
}
#endif
