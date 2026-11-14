#include "veriobjc_runtime.h"
#include "vstd/simd.h"

veriobjc_nolatency int32_t run_valid_vstd_simd(void) {
    int32_t a[4] = {1, 2, 3, 4};
    int32_t b[4] = {10, 20, 30, 40};
    vsimd_i32x4 va = vsimd_load_i32x4(a);
    vsimd_i32x4 vb = vsimd_load_i32x4(b);
    vsimd_i32x4 vsum = vsimd_add_i32x4(va, vb);
    return vsimd_reduce_add_i32x4(vsum);
}
