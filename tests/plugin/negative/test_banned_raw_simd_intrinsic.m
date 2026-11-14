// EXPECTED_DIAGNOSTIC: VeriObjC Violation: Raw SIMD intrinsic in @nolatency code is banned
#include "veriobjc_runtime.h"

static inline int32_t _mm_raw_vendor_intrinsic_sim(int32_t a, int32_t b) {
    return a + b;
}

veriobjc_nolatency int32_t call_raw_intrinsic_in_nolatency(int32_t x) {
    return _mm_raw_vendor_intrinsic_sim(x, 1);
}
