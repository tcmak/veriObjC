// examples/vio_loopback_ring.m
// Demonstrates Task 3.11 (<vstd/io.h>):
//   - Fixed-size non-blocking DMA ring registration (vdma_ring_def)
//   - In-process loopback simulation transport (vio_loopback_roundtrip)
#include "veriobjc_runtime.h"
#include "vstd/io.h"

vdma_ring_def(LoopbackRing4, 16, 4);

int32_t run_io_loopback_demo(void) {
    LoopbackRing4 ring;
    vdma_ring_init(&ring);
    uint8_t tx_frame[4] = {10, 20, 30, 40};
    uint8_t rx_frame[16] = {0};
    uint32_t rx_len = 0u;
    if (!vio_loopback_roundtrip(&ring, tx_frame, 4u, rx_frame, sizeof(rx_frame), &rx_len)) {
        return -1;
    }
    return (int32_t)(rx_frame[0] + rx_frame[3]);
}

#ifdef VERIOBJC_EXAMPLE_MAIN
int main(void) {
    return (run_io_loopback_demo() == 50) ? 0 : 1;
}
#endif
