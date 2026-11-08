#include "veriobjc_runtime.h"

vconstexpr uint32_t next_power_of_two(uint32_t v) {
    uint32_t p = 1u;
    while (p < v) {
        p = p << 1u;
    }
    return p;
}

vconstexpr uint32_t compute_fee_bps(uint32_t tier, uint32_t base_bps) {
    if (tier == 0u) {
        return base_bps;
    }
    return compute_fee_bps(tier - 1u, base_bps - 2u);
}

uint32_t configure_venue_ring_and_fee(uint32_t *out_mask) {
    if (out_mask) {
        *out_mask = next_power_of_two(1000u) - 1u;
    }
    return compute_fee_bps(3u, 15u);
}
