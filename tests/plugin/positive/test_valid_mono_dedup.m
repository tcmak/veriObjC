// Positive test: Monomorphization deduplication across identical blocks & --mono-report
#include "veriobjc_runtime.h"

int32_t apply_deduped_blocks(int32_t x) {
    vmono_instantiate("core_math_pkg", "ClampGeneric<int32_t>", 128, 16);
    vmono_instantiate("core_math_pkg", "ClampGeneric<int32_t>", 128, 16); // Deduplicated!

    int32_t (^stepA)(int32_t) = ^int32_t(int32_t v) {
        return v + 1;
    };
    int32_t (^stepB)(int32_t) = ^int32_t(int32_t v) {
        return v + 1;
    };
    return stepA(x) + stepB(x);
}
