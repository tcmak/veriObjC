#pragma once
#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <vstd/channel.h> — Lock-Free SPSC Flat-Value Ring Channels (vspsc_channel)
// Layer: vstd (depends on: rt)
//
// Invariants:
//   1. Payloads crossing a channel must be flat values (crossing = byte copy).
//   2. Single-Producer / Single-Consumer: VeriObjCPlugin rejects multiple
//      distinct producer functions writing to the same SPSC channel.
//   3. Acquire/Release memory ordering guarantees lock-free publication.
// ---------------------------------------------------------------------------

#define vspsc_channel_def(ChanName, ElemType, CapPow2) \
    typedef struct __attribute__((annotate("objc_spsc_channel:" #CapPow2), annotate("objc_flat"))) { \
        uint32_t head; \
        uint32_t tail; \
        uint32_t cap; \
        ElemType slots[CapPow2]; \
    } ChanName

static inline void __vspsc_channel_init_raw(
    void *chan_obj,
    uint32_t *head_ptr,
    uint32_t *tail_ptr,
    uint32_t *cap_ptr,
    uint32_t cap_val
) {
    if (!chan_obj) {
        return;
    }
    __atomic_store_n(head_ptr, 0u, __ATOMIC_RELAXED);
    __atomic_store_n(tail_ptr, 0u, __ATOMIC_RELAXED);
    *cap_ptr = cap_val;
}

static inline bool __vspsc_channel_send_raw(
    void *chan_obj,
    uint32_t *head_ptr,
    const uint32_t *tail_ptr,
    uint32_t cap_val,
    void *slots_ptr,
    size_t elem_size,
    const void *val_ptr
) {
    if (!chan_obj) {
        return false;
    }
    uint32_t head = __atomic_load_n(head_ptr, __ATOMIC_RELAXED);
    uint32_t tail = __atomic_load_n(tail_ptr, __ATOMIC_ACQUIRE);
    if ((head - tail) >= cap_val) {
        return false;
    }
    uint32_t idx = head & (cap_val - 1u);
    uint8_t *dst = (uint8_t *)slots_ptr + (size_t)idx * elem_size;
    const uint8_t *src = (const uint8_t *)val_ptr;
    for (size_t i = 0u; i < elem_size; ++i) {
        dst[i] = src[i];
    }
    __atomic_store_n(head_ptr, head + 1u, __ATOMIC_RELEASE);
    return true;
}

static inline bool __vspsc_channel_recv_raw(
    void *chan_obj,
    const uint32_t *head_ptr,
    uint32_t *tail_ptr,
    uint32_t cap_val,
    const void *slots_ptr,
    size_t elem_size,
    void *out_val_ptr
) {
    if (!chan_obj || !out_val_ptr) {
        return false;
    }
    uint32_t tail = __atomic_load_n(tail_ptr, __ATOMIC_RELAXED);
    uint32_t head = __atomic_load_n(head_ptr, __ATOMIC_ACQUIRE);
    if (tail == head) {
        return false;
    }
    uint32_t idx = tail & (cap_val - 1u);
    const uint8_t *src = (const uint8_t *)slots_ptr + (size_t)idx * elem_size;
    uint8_t *dst = (uint8_t *)out_val_ptr;
    for (size_t i = 0u; i < elem_size; ++i) {
        dst[i] = src[i];
    }
    __atomic_store_n(tail_ptr, tail + 1u, __ATOMIC_RELEASE);
    return true;
}

#ifdef __cplusplus
#define __vspsc_val_ptr(ElemType, val) (&static_cast<const ElemType &>(val))
#else
#define __vspsc_val_ptr(ElemType, val) ((const void *)(const ElemType[1]){ (val) })
#endif

#define vspsc_channel_init(chan_ptr) \
    __vspsc_channel_init_raw( \
        (chan_ptr), \
        (chan_ptr) ? &(chan_ptr)->head : (uint32_t *)NULL, \
        (chan_ptr) ? &(chan_ptr)->tail : (uint32_t *)NULL, \
        (chan_ptr) ? &(chan_ptr)->cap : (uint32_t *)NULL, \
        (uint32_t)(sizeof((chan_ptr)->slots) / sizeof((chan_ptr)->slots[0])))

#define vspsc_channel_send(chan_ptr, val) \
    __vspsc_channel_send_raw( \
        (chan_ptr), \
        (chan_ptr) ? &(chan_ptr)->head : (uint32_t *)NULL, \
        (chan_ptr) ? &(chan_ptr)->tail : (const uint32_t *)NULL, \
        (chan_ptr) ? (chan_ptr)->cap : 0u, \
        (chan_ptr) ? (void *)(chan_ptr)->slots : NULL, \
        sizeof((chan_ptr)->slots[0]), \
        __vspsc_val_ptr(__typeof__((chan_ptr)->slots[0]), (val)))

#define vspsc_channel_recv(chan_ptr, out_ptr) \
    __vspsc_channel_recv_raw( \
        (chan_ptr), \
        (chan_ptr) ? &(chan_ptr)->head : (const uint32_t *)NULL, \
        (chan_ptr) ? &(chan_ptr)->tail : (uint32_t *)NULL, \
        (chan_ptr) ? (chan_ptr)->cap : 0u, \
        (chan_ptr) ? (const void *)(chan_ptr)->slots : NULL, \
        sizeof((chan_ptr)->slots[0]), \
        (void *)(out_ptr))

#ifdef __cplusplus
}
#endif
