// Positive plugin test (Task 4.4): real Z3 QF_BV formal contract discharge (--smt-verify / --smt-report)
#include "veriobjc_runtime.h"
#include "vstd/contracts.h"

// 1. Bounded affine scaling in exact 32-bit signed arithmetic (no overflow possible for 0 <= qty <= 1000)
vcontract_pre("qty >= 0 && qty <= 1000")
vcontract_post("result >= 16 && result <= 2016")
veriobjc_export veriobjc_pure int32_t scale_order_lot_fee(int32_t qty) {
    int32_t scaled = qty * 2 + 16;
    return scaled;
}

// 2. Branching clamp using <vstd/contracts.h> @pure helper vcontract_in_range_i64
vcontract_pre("raw_bps >= -50000 && raw_bps <= 50000")
vcontract_post("result >= 0 && result <= 10000")
veriobjc_export veriobjc_pure int64_t clamp_fee_bps(int64_t raw_bps) {
    if (vcontract_in_range_i64(raw_bps, 0, 10000)) {
        return raw_bps;
    }
    if (raw_bps < 0) {
        return 0;
    }
    return 10000;
}

// 3. Bitwise 64-byte DMA cacheline alignment in exact 32-bit unsigned bit-vector arithmetic
vcontract_pre("payload_len <= 4096")
vcontract_post("(result & 63) == 0 && result >= payload_len && result <= 4096")
veriobjc_export veriobjc_pure uint32_t align_dma_len_64(uint32_t payload_len) {
    return (payload_len + 63u) & ~63u;
}
