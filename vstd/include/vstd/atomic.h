#pragma once
#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <vstd/atomic.h> — Scoped Linear Atomic Guards (vatomic<T>)
// Layer: vstd (depends on: rt)
//
// Invariants:
//   1. Direct access to payload_DoNotAccessDirectly is rejected by VeriObjCPlugin.
//   2. vatomic_acquire(guard_var, atomic_ptr) acquires with __ATOMIC_ACQUIRE
//      and automatically releases with __ATOMIC_RELEASE at scope exit.
// ---------------------------------------------------------------------------

#define vatomic_def(AtomicName, PayloadType) \
    typedef struct __attribute__((annotate("objc_vatomic"), annotate("objc_flat"))) { \
        uint32_t    lock_state; \
        PayloadType payload_DoNotAccessDirectly; \
    } AtomicName

struct vatomic_guard_raw {
    uint32_t *lock_ptr;
    int32_t  *payload_ptr;
};

static inline void __vatomic_init_raw(
    void *atomic_obj,
    uint32_t *lock_ptr,
    size_t payload_offset,
    size_t payload_size,
    const void *init_val_ptr
) {
    if (!atomic_obj) {
        return;
    }
    __atomic_store_n(lock_ptr, 0u, __ATOMIC_RELAXED);
    uint8_t *dst = (uint8_t *)atomic_obj + payload_offset;
    const uint8_t *src = (const uint8_t *)init_val_ptr;
    for (size_t i = 0u; i < payload_size; ++i) {
        dst[i] = src[i];
    }
}

static inline struct vatomic_guard_raw __vatomic_acquire_raw(
    void *atomic_obj,
    uint32_t *lock_ptr,
    size_t payload_offset
) {
    if (!atomic_obj) {
        struct vatomic_guard_raw empty = { .lock_ptr = NULL, .payload_ptr = NULL };
        return empty;
    }
    (void)__atomic_exchange_n(lock_ptr, 1u, __ATOMIC_ACQUIRE);
    struct vatomic_guard_raw g = {
        .lock_ptr = lock_ptr,
        .payload_ptr = (int32_t *)((uint8_t *)atomic_obj + payload_offset)
    };
    return g;
}

static inline void __vatomic_release_cleanup(struct vatomic_guard_raw *g) {
    if (g->lock_ptr) {
        __atomic_store_n(g->lock_ptr, 0u, __ATOMIC_RELEASE);
        g->lock_ptr = NULL;
    }
}

static inline int32_t *__vatomic_get_raw(const struct vatomic_guard_raw *g) {
    return g->payload_ptr;
}

#ifdef __cplusplus
#define __vatomic_val_ptr(ElemType, val) (&static_cast<const ElemType &>(val))
#else
#define __vatomic_val_ptr(ElemType, val) ((const void *)(const ElemType[1]){ (val) })
#endif

#define vatomic_init(atomic_ptr, init_val) \
    __vatomic_init_raw( \
        (atomic_ptr), \
        (atomic_ptr) ? &(atomic_ptr)->lock_state : (uint32_t *)NULL, \
        __builtin_offsetof(__typeof__(*(atomic_ptr)), payload_DoNotAccessDirectly), \
        sizeof(*(atomic_ptr)) - __builtin_offsetof(__typeof__(*(atomic_ptr)), payload_DoNotAccessDirectly), \
        __vatomic_val_ptr(int32_t, (init_val)))

#define vatomic_acquire(guard_var, atomic_ptr) \
    struct vatomic_guard_raw guard_var __attribute__((cleanup(__vatomic_release_cleanup))) = \
        __vatomic_acquire_raw( \
            (atomic_ptr), \
            (atomic_ptr) ? &(atomic_ptr)->lock_state : (uint32_t *)NULL, \
            __builtin_offsetof(__typeof__(*(atomic_ptr)), payload_DoNotAccessDirectly))

#define vatomic_get(guard_ptr) \
    __vatomic_get_raw((guard_ptr))

#ifdef __cplusplus
}
#endif
