// Positive plugin test (Task 6.0.4): canonical VeriObjC attribute macros, container prefixes,
// contract annotations, and unrelated third-party annotate("...") tags must all pass cleanly.
#include "veriobjc_runtime.h"
#include "vstd/contracts.h"
#include "vstd/vector.h"
#include "vstd/io.h"

VVector_decl(int32_t, 4);
vdma_ring_def(CanonicalDmaRing4, 64, 4);

veriobjc_cold veriobjc_effect_io
int32_t canonical_cold_entry(int32_t status) {
    return status;
}

vcontract_pre("x >= 0 && x <= 100")
vcontract_post("result >= 1 && result <= 101")
veriobjc_export veriobjc_pure
int32_t canonical_pure_inc(int32_t x) {
    return x + 1;
}

// Non-VeriObjC third-party annotation prefix must not be rejected
__attribute__((annotate("third_party_profiler_hook")))
veriobjc_nolatency
int32_t canonical_nolatency_step(void) {
    VVector(int32_t, 4) vec;
    vvector_init(&vec);
    vvector_push(&vec, 7);

    CanonicalDmaRing4 ring;
    vdma_ring_init(&ring);
    int32_t *first = vvector_at(&vec, 0u);
    return (first ? *first : 0) + (int32_t)ring.head;
}
