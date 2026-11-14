#pragma once
#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <vstd/io.h> — Non-Blocking DMA Descriptor Rings & In-Process Loopback I/O
// Layer: vstd (depends on: rt)
//
// Invariants:
//   1. All operations are strictly non-blocking (zero POSIX read/write calls).
//   2. Fixed-size DMA lines are pre-registered in a power-of-two ring; ring
//      overflow is rejected at compile time by VeriObjCPlugin and returns
//      false at runtime (never drops silently).
//   3. vio_loopback_roundtrip implements the identical ring API for local
//      simulation before bare-metal flashing.
// ---------------------------------------------------------------------------

#define vdma_ring_def(RingName, LineSize, RingCap) \
    typedef struct __attribute__((annotate("objc_vdma_ring:" #RingCap), annotate("objc_flat"))) { \
        uint32_t head; \
        uint32_t tail; \
        uint32_t ring_cap; \
        uint32_t line_size; \
        uint32_t frame_lens[RingCap]; \
        uint8_t  lines[RingCap][LineSize]; \
    } RingName

static inline void __vdma_ring_init_raw(
    void *ring_obj,
    uint32_t *head_ptr,
    uint32_t *tail_ptr,
    uint32_t *cap_ptr,
    uint32_t *line_size_ptr,
    uint32_t cap_val,
    uint32_t line_size_val
) {
    if (!ring_obj) {
        return;
    }
    *head_ptr = 0u;
    *tail_ptr = 0u;
    *cap_ptr = cap_val;
    *line_size_ptr = line_size_val;
}

static inline bool __vdma_ring_submit_raw(
    void *ring_obj,
    uint32_t *head_ptr,
    uint32_t tail_val,
    uint32_t cap_val,
    uint32_t line_size_val,
    uint32_t *frame_lens,
    uint8_t *lines_base,
    const uint8_t *payload,
    uint32_t payload_len
) {
    if (!ring_obj || !payload || payload_len > line_size_val || (*head_ptr - tail_val) >= cap_val) {
        return false;
    }
    uint32_t slot = *head_ptr & (cap_val - 1u);
    uint8_t *dst = lines_base + (size_t)slot * (size_t)line_size_val;
    for (uint32_t i = 0u; i < payload_len; ++i) {
        dst[i] = payload[i];
    }
    frame_lens[slot] = payload_len;
    (*head_ptr)++;
    return true;
}

static inline bool __vdma_ring_poll_raw(
    void *ring_obj,
    uint32_t head_val,
    uint32_t *tail_ptr,
    uint32_t cap_val,
    uint32_t line_size_val,
    const uint32_t *frame_lens,
    const uint8_t *lines_base,
    uint8_t *out_buf,
    uint32_t out_cap,
    uint32_t *out_len
) {
    if (!ring_obj || !out_buf || !out_len || *tail_ptr == head_val) {
        return false;
    }
    uint32_t slot = *tail_ptr & (cap_val - 1u);
    uint32_t flen = frame_lens[slot];
    if (flen > out_cap) {
        return false;
    }
    const uint8_t *src = lines_base + (size_t)slot * (size_t)line_size_val;
    for (uint32_t i = 0u; i < flen; ++i) {
        out_buf[i] = src[i];
    }
    *out_len = flen;
    (*tail_ptr)++;
    return true;
}

#define vdma_ring_init(ring_ptr) \
    __vdma_ring_init_raw( \
        (ring_ptr), \
        (ring_ptr) ? &(ring_ptr)->head : (uint32_t *)NULL, \
        (ring_ptr) ? &(ring_ptr)->tail : (uint32_t *)NULL, \
        (ring_ptr) ? &(ring_ptr)->ring_cap : (uint32_t *)NULL, \
        (ring_ptr) ? &(ring_ptr)->line_size : (uint32_t *)NULL, \
        (uint32_t)(sizeof((ring_ptr)->lines) / sizeof((ring_ptr)->lines[0])), \
        (uint32_t)(sizeof((ring_ptr)->lines[0]) / sizeof((ring_ptr)->lines[0][0])))

#define vdma_ring_submit(ring_ptr, payload_ptr, payload_len) \
    __vdma_ring_submit_raw( \
        (ring_ptr), \
        (ring_ptr) ? &(ring_ptr)->head : (uint32_t *)NULL, \
        (ring_ptr) ? (ring_ptr)->tail : 0u, \
        (ring_ptr) ? (ring_ptr)->ring_cap : 0u, \
        (ring_ptr) ? (ring_ptr)->line_size : 0u, \
        (ring_ptr) ? (ring_ptr)->frame_lens : (uint32_t *)NULL, \
        (ring_ptr) ? &(ring_ptr)->lines[0][0] : (uint8_t *)NULL, \
        (const uint8_t *)(payload_ptr), \
        (uint32_t)(payload_len))

#define vdma_ring_poll(ring_ptr, out_buf, out_cap, out_len_ptr) \
    __vdma_ring_poll_raw( \
        (ring_ptr), \
        (ring_ptr) ? (ring_ptr)->head : 0u, \
        (ring_ptr) ? &(ring_ptr)->tail : (uint32_t *)NULL, \
        (ring_ptr) ? (ring_ptr)->ring_cap : 0u, \
        (ring_ptr) ? (ring_ptr)->line_size : 0u, \
        (ring_ptr) ? (ring_ptr)->frame_lens : (const uint32_t *)NULL, \
        (ring_ptr) ? &(ring_ptr)->lines[0][0] : (const uint8_t *)NULL, \
        (uint8_t *)(out_buf), \
        (uint32_t)(out_cap), \
        (out_len_ptr))

#define vio_loopback_roundtrip(ring_ptr, tx_buf, tx_len, rx_buf, rx_cap, out_rx_len_ptr) \
    (vdma_ring_submit((ring_ptr), (tx_buf), (tx_len)) && \
     vdma_ring_poll((ring_ptr), (rx_buf), (rx_cap), (out_rx_len_ptr)))

#ifdef __cplusplus
}
#endif
