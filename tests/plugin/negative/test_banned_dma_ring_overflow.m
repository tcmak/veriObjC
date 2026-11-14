// EXPECTED_DIAGNOSTIC: VeriObjC Violation: DMA ring capacity exceeded
#include "veriobjc_runtime.h"
#include "vstd/io.h"

vdma_ring_def(TinyDmaRing2, 8, 2);

void overflow_dma_ring(void) {
    TinyDmaRing2 ring;
    vdma_ring_init(&ring);
    uint8_t pkt[4] = {1, 2, 3, 4};
    vdma_ring_submit(&ring, pkt, 4u);
    vdma_ring_submit(&ring, pkt, 4u);
    // 3rd submit on a capacity-2 DMA ring without draining is rejected at compile time
    vdma_ring_submit(&ring, pkt, 4u);
}
