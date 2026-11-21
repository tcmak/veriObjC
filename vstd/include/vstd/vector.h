#pragma once
#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <vstd/vector.h> — Fixed-Capacity Inplace Containers (VVector_decl / VVector)
// Layer: vstd (depends on: rt)
//
// Invariants:
//   1. Elements live inline inside the container footprint (zero heap allocation).
//   2. Pushing beyond capacity is rejected at compile time by VeriObjCPlugin
//      (via AST inspection of annotate("veriobjc_vvector")) and returns false at runtime.
//   3. Both vvector_push and vvector_push_inplace construct elements directly
//      in the destination slot without compound-literal temporaries or memcpy.
//   4. Pure C11/C23/ObjC/C++ AST-checked macro surface (zero source-text rewriting):
//      - 2-arg VVector_decl(ElemIdent, N) + VVector(ElemIdent, N) for single-token types
//      - 3-arg VVector_decl(TypeName, ElemType, N) for arbitrary/pointer types & capacities N
//      - Constrained vgeneric @interface VVector<T : VVectorItem *> for ObjC pointer views
// ---------------------------------------------------------------------------

#ifdef __cplusplus
#define __VERIOBJC_VVEC_STATIC_ASSERT(cond, msg) static_assert((cond), msg)
#else
#define __VERIOBJC_VVEC_STATIC_ASSERT(cond, msg) _Static_assert((cond), msg)
#endif

#define __VERIOBJC_VVEC_TAG(ElemIdent, Cap) \
    VERIOBJC_CONCAT(VERIOBJC_CONCAT(__veriobjc_vvec_, ElemIdent), VERIOBJC_CONCAT(_, Cap))

#define __VERIOBJC_VVEC_DECL_2(ElemIdent, Cap) \
    struct __attribute__((annotate("veriobjc_vvector"))) __VERIOBJC_VVEC_TAG(ElemIdent, Cap) { \
        __VERIOBJC_VVEC_STATIC_ASSERT((Cap) > 0, "VVector capacity N must be greater than zero"); \
        uint32_t count; \
        uint32_t cap; \
        ElemIdent buf[Cap]; \
    }

#define __VERIOBJC_VVEC_DECL_3(TypeName, ElemType, Cap) \
    typedef struct __attribute__((annotate("veriobjc_vvector"))) TypeName { \
        __VERIOBJC_VVEC_STATIC_ASSERT((Cap) > 0, "VVector capacity N must be greater than zero"); \
        uint32_t count; \
        uint32_t cap; \
        ElemType buf[Cap]; \
    } TypeName

#define __VERIOBJC_VVEC_GET_MACRO(_1, _2, _3, NAME, ...) NAME

#define VVector_decl(...) \
    __VERIOBJC_VVEC_GET_MACRO(__VA_ARGS__, __VERIOBJC_VVEC_DECL_3, __VERIOBJC_VVEC_DECL_2)(__VA_ARGS__)

#define VVector(ElemIdent, Cap) \
    struct __VERIOBJC_VVEC_TAG(ElemIdent, Cap)

#define vinterface_by_value(InterfaceName) \
    struct __attribute__((annotate("objc_interface_by_value:" #InterfaceName))) { \
        uint32_t __vobj_byval_slot; \
    }

static inline void __vvector_init_raw(void *vec_obj, uint32_t *count_ptr, uint32_t *cap_ptr, uint32_t cap_val) {
    if (!vec_obj) {
        return;
    }
    *count_ptr = 0u;
    *cap_ptr = cap_val;
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

#define vvector_init(vec_ptr) \
    __vvector_init_raw( \
        (vec_ptr), \
        (vec_ptr) ? &(vec_ptr)->count : (uint32_t *)NULL, \
        (vec_ptr) ? &(vec_ptr)->cap : (uint32_t *)NULL, \
        (uint32_t)(sizeof((vec_ptr)->buf) / sizeof((vec_ptr)->buf[0])))

#define vvector_push(vec_ptr, val) \
    ((__vvector_alloc_slot_raw( \
        (vec_ptr), \
        (vec_ptr) ? &(vec_ptr)->count : (uint32_t *)NULL, \
        (vec_ptr) ? (vec_ptr)->cap : 0u, \
        (vec_ptr) ? (void *)(vec_ptr)->buf : NULL, \
        sizeof((vec_ptr)->buf[0])) != NULL) \
        ? ((vec_ptr)->buf[(vec_ptr)->count - 1u] = (val), true) \
        : false)

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

#ifdef __OBJC__
#define VVECTOR_GENERIC_CAPACITY 4u

__attribute__((objc_root_class))
__attribute__((annotate("objc_sealed")))
@interface VVectorItem {
@public
    uint32_t item_tag;
}
@end

__attribute__((objc_root_class))
__attribute__((annotate("objc_sealed")))
__attribute__((annotate("objc_final")))
__attribute__((annotate("objc_vvector:4")))
@interface VVector<T : VVectorItem *> {
@public
    uint32_t count;
    uint32_t cap;
    T buf[VVECTOR_GENERIC_CAPACITY];
}
- (void)initVec;
- (bool)push:(T)item;
- (T)at:(uint32_t)idx;
- (uint32_t)len;
- (uint32_t)cap;
@end

static inline void __veriobjc_direct_VVector__initVec(VVector *self) {
    if (!self) return;
    self->count = 0u;
    self->cap = VVECTOR_GENERIC_CAPACITY;
    for (uint32_t i = 0u; i < VVECTOR_GENERIC_CAPACITY; ++i) {
        self->buf[i] = (VVectorItem *)0;
    }
}

static inline bool __veriobjc_direct_VVector__push_(VVector *self, VVectorItem *item) {
    if (!self || self->count >= VVECTOR_GENERIC_CAPACITY) {
        return false;
    }
    self->buf[self->count] = item;
    self->count++;
    return true;
}

static inline VVectorItem *__veriobjc_direct_VVector__at_(VVector *self, uint32_t idx) {
    if (!self || idx >= self->count) {
        return (VVectorItem *)0;
    }
    return self->buf[idx];
}

static inline uint32_t __veriobjc_direct_VVector__len(VVector *self) {
    return self ? self->count : 0u;
}

static inline uint32_t __veriobjc_direct_VVector__cap(VVector *self) {
    return self ? self->cap : 0u;
}
#endif
