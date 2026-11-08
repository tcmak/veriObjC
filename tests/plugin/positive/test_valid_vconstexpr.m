// Positive test: vconstexpr function evaluated by the fuel-bounded interpreter and folded into an IntegerLiteral
#include "veriobjc_runtime.h"

vconstexpr uint32_t round_up_pow2(uint32_t x) {
    uint32_t p = 1u;
    while (p < x) {
        p = p << 1u;
    }
    return p;
}

vconstexpr uint32_t ring_index_mask(uint32_t min_capacity) {
    return round_up_pow2(min_capacity) - 1u;
}

uint32_t get_order_ring_mask(void) {
    return ring_index_mask(300u);
}
