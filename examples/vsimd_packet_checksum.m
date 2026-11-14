// examples/vsimd_packet_checksum.m
// Demonstrates Task 3.13 (<vstd/simd.h>):
//   - Hardware-aware portable SIMD vectorization in @nolatency code without raw intrinsics
#include "veriobjc_runtime.h"
#include "vstd/simd.h"

veriobjc_nolatency int32_t run_simd_checksum_demo(void) {
    int32_t hdr_words[4] = {100, 200, 300, 400};
    int32_t body_words[4] = {1, 2, 3, 4};
    vsimd_i32x4 vhdr = vsimd_load_i32x4(hdr_words);
    vsimd_i32x4 vbody = vsimd_load_i32x4(body_words);
    vsimd_i32x4 vsum = vsimd_add_i32x4(vhdr, vbody);
    return vsimd_reduce_add_i32x4(vsum);
}

#ifdef VERIOBJC_EXAMPLE_MAIN
int main(void) {
    return (run_simd_checksum_demo() == 1010) ? 0 : 1;
}
#endif
