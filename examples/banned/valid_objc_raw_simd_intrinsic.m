// EXPECTED_DIAGNOSTIC: VeriObjC Violation: Raw SIMD intrinsic in @nolatency code is banned
// Valid C/Objective-C function call, banned in VeriObjC @nolatency contexts where <vstd/simd.h> must be used.
#include "veriobjc_runtime.h"

static inline int32_t _mm_raw_vendor_intrinsic_sim(int32_t a, int32_t b) {
    return a + b;
}

__attribute__((annotate("objc_nolatency"))) int32_t call_raw_intrinsic_in_nolatency(int32_t x) {
    return _mm_raw_vendor_intrinsic_sim(x, 1);
}
