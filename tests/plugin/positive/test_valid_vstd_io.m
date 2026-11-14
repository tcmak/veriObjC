#include "veriobjc_runtime.h"
#include "vstd/io.h"

vdma_ring_def(NicRing4, 16, 4);

int32_t run_valid_vstd_io(void) {
    NicRing4 ring;
    vdma_ring_init(&ring);
    uint8_t tx[4] = {10, 20, 30, 40};
    uint8_t rx[16] = {0};
    uint32_t rx_len = 0u;
    if (!vio_loopback_roundtrip(&ring, tx, 4u, rx, sizeof(rx), &rx_len)) {
        return -1;
    }
    return (int32_t)rx[0];
}
