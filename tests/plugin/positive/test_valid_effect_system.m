// Positive test: Effect system (@nolatency, @pure, @cold) and --effects-report survivor listing
#include "veriobjc_runtime.h"

veriobjc_effect_fence
static inline void hw_store_fence(uint64_t *slot, uint64_t v) {
    *slot = v;
}

veriobjc_effect_io
static inline void cold_diag_log(uint32_t code) {
    (void)code;
}

veriobjc_pure
uint32_t compute_checksum(uint32_t a, uint32_t b) {
    return (a ^ b) + 0x9e3779b9u;
}

veriobjc_nolatency
uint32_t fast_ring_publish(uint64_t *slot, uint32_t a, uint32_t b) {
    uint32_t crc = compute_checksum(a, b);
    hw_store_fence(slot, (uint64_t)crc);
    return crc;
}

veriobjc_cold
void cold_audit_entry(uint32_t code) {
    cold_diag_log(code);
}
