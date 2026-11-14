// EXPECTED_DIAGNOSTIC: VeriObjC Violation: DMA ring capacity exceeded
// Valid C/Objective-C calls, banned in VeriObjC when submitting more frames than a DMA ring's static capacity.
#include "veriobjc_runtime.h"
#include "vstd/io.h"

vdma_ring_def(TinyDmaRing2, 8, 2);

void overflow_dma_ring(void) {
    TinyDmaRing2 ring;
    vdma_ring_init(&ring);
    uint8_t pkt[4] = {1, 2, 3, 4};
    vdma_ring_submit(&ring, pkt, 4u);
    vdma_ring_submit(&ring, pkt, 4u);
    vdma_ring_submit(&ring, pkt, 4u);
}
