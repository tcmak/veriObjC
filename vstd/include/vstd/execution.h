#pragma once
#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <vstd/execution.h> — P2300-Style Stack Senders & Interrupt-Woken vasync
// Layer: vstd (depends on: rt)
//
// Invariants:
//   1. Senders/receivers are immutable value structs on the caller's stack.
//   2. vasync futures are woken exclusively via hardware interrupt hooks
//      (vasync_irq_wake); busy-wait polling loops on vasync_task_is_ready
//      are rejected at compile time by VeriObjCPlugin.
// ---------------------------------------------------------------------------

typedef int32_t (*vsender_step_i32_fn)(int32_t);

struct vsender_i32 {
    int32_t value;
};

static inline struct vsender_i32 vsender_just_i32(int32_t val) {
    struct vsender_i32 s = { .value = val };
    return s;
}

static inline struct vsender_i32 vsender_then_i32(struct vsender_i32 prev, vsender_step_i32_fn fn) {
    if (!fn) {
        return prev;
    }
    struct vsender_i32 next = { .value = fn(prev.value) };
    return next;
}

static inline int32_t vsender_sync_wait_i32(struct vsender_i32 s) {
    return s.value;
}

#define vasync_task_def(TaskName, ResultType) \
    typedef struct __attribute__((annotate("objc_vasync_task"), annotate("objc_flat"))) { \
        uint32_t   irq_vector; \
        uint32_t   ready_flag; \
        ResultType result_val; \
    } TaskName

static inline void __vasync_task_init_raw(
    void *task_obj,
    uint32_t *irq_ptr,
    uint32_t *ready_ptr,
    void *result_ptr,
    size_t result_size,
    uint32_t irq_vec
) {
    if (!task_obj) {
        return;
    }
    *irq_ptr = irq_vec;
    __atomic_store_n(ready_ptr, 0u, __ATOMIC_RELAXED);
    uint8_t *dst = (uint8_t *)result_ptr;
    for (size_t i = 0u; i < result_size; ++i) {
        dst[i] = 0u;
    }
}

static inline void __vasync_irq_wake_raw(
    void *task_obj,
    uint32_t *ready_ptr,
    void *result_ptr,
    size_t result_size,
    const void *val_ptr
) {
    if (!task_obj) {
        return;
    }
    uint8_t *dst = (uint8_t *)result_ptr;
    const uint8_t *src = (const uint8_t *)val_ptr;
    for (size_t i = 0u; i < result_size; ++i) {
        dst[i] = src[i];
    }
    __atomic_store_n(ready_ptr, 1u, __ATOMIC_RELEASE);
}

static inline bool __vasync_task_is_ready_raw(const void *task_obj, const uint32_t *ready_ptr) {
    if (!task_obj) {
        return false;
    }
    return __atomic_load_n(ready_ptr, __ATOMIC_ACQUIRE) != 0u;
}

#ifdef __cplusplus
#define __vasync_val_ptr(ElemType, val) (&static_cast<const ElemType &>(val))
#else
#define __vasync_val_ptr(ElemType, val) ((const void *)(const ElemType[1]){ (val) })
#endif

#define vasync_task_init(task_ptr, irq_vec) \
    __vasync_task_init_raw( \
        (task_ptr), \
        (task_ptr) ? &(task_ptr)->irq_vector : (uint32_t *)NULL, \
        (task_ptr) ? &(task_ptr)->ready_flag : (uint32_t *)NULL, \
        (task_ptr) ? (void *)&(task_ptr)->result_val : NULL, \
        sizeof((task_ptr)->result_val), \
        (uint32_t)(irq_vec))

#define vasync_irq_wake(task_ptr, val) \
    __vasync_irq_wake_raw( \
        (task_ptr), \
        (task_ptr) ? &(task_ptr)->ready_flag : (uint32_t *)NULL, \
        (task_ptr) ? (void *)&(task_ptr)->result_val : NULL, \
        sizeof((task_ptr)->result_val), \
        __vasync_val_ptr(__typeof__((task_ptr)->result_val), (val)))

#define vasync_task_is_ready(task_ptr) \
    __vasync_task_is_ready_raw( \
        (task_ptr), \
        (task_ptr) ? &(task_ptr)->ready_flag : (const uint32_t *)NULL)

#define vtask_dag_edge(from_node, to_node, wcet_cycles) \
    __veriobjc_dag_edge((from_node), (to_node), (wcet_cycles))

#define vtask_dag_critical_path_wcet() \
    __veriobjc_dag_critical_path_wcet()

#ifdef __cplusplus
}
#endif
