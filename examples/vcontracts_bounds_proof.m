// examples/vcontracts_bounds_proof.m
// Demonstrates Tasks 3.14 & 4.4 (<vstd/contracts.h> + real Z3 QF_BV contract discharge):
//   - Pure (@pure) contract predicates (0 surviving effects)
//   - Exact C bit-width @pre / @post contracts proven UNSAT_PROVEN by z3 -smt2 -in
#include "veriobjc_runtime.h"
#include "vstd/contracts.h"

vcontract_pre("price >= 1 && price <= 100000")
vcontract_post("result >= 1 && result <= 100000")
veriobjc_export veriobjc_pure int64_t clamp_limit_price(int64_t price) {
    if (!vcontract_in_range_i64(price, 1, 100000)) {
        return 1;
    }
    return price;
}

vcontract_pre("bytes <= 8192")
vcontract_post("(result & 63) == 0 && result >= bytes && result <= 8192")
veriobjc_export veriobjc_pure uint32_t round_up_dma_frame_64(uint32_t bytes) {
    return (bytes + 63u) & ~63u;
}

#ifdef VERIOBJC_EXAMPLE_MAIN
int main(void) {
    return (clamp_limit_price(500) == 500 &&
            clamp_limit_price(-5) == 1 &&
            round_up_dma_frame_64(65u) == 128u) ? 0 : 1;
}
#endif
