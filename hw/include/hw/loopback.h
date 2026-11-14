#pragma once
#include "veriobjc_runtime.h"
#include "sys0/mem.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <hw/loopback.h> — Driver-as-Library Non-Blocking UART / NIC Loopback
// Layer: hw (depends on: rt, sys0)
// ---------------------------------------------------------------------------

#define HW_UART_FIFO_CAPACITY 64u

struct hw_uart_loopback_dev {
    uint32_t rx_available;
    uint8_t  fifo[HW_UART_FIFO_CAPACITY];
};

static inline void hw_uart_loopback_init(struct hw_uart_loopback_dev *dev) {
    if (!dev) {
        return;
    }
    dev->rx_available = 0u;
    sys0_memset(dev->fifo, 0, HW_UART_FIFO_CAPACITY);
}

static inline bool hw_uart_write_nonblock(struct hw_uart_loopback_dev *dev, const void *src, size_t len) {
    if (!dev || !src || len > HW_UART_FIFO_CAPACITY) {
        return false;
    }
    sys0_memcpy(dev->fifo, src, len);
    dev->rx_available = (uint32_t)len;
    return true;
}

static inline bool hw_uart_read_nonblock(
    struct hw_uart_loopback_dev *dev,
    void *dst,
    size_t dst_cap,
    size_t *out_read
) {
    if (!dev || !dst || !out_read || dev->rx_available == 0u || (size_t)dev->rx_available > dst_cap) {
        return false;
    }
    size_t n = (size_t)dev->rx_available;
    sys0_memcpy(dst, dev->fifo, n);
    dev->rx_available = 0u;
    *out_read = n;
    return true;
}

#ifdef __cplusplus
}
#endif
