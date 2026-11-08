// Positive test: non-escaping function block monomorphized into direct static C call (0 _NSConcreteStackBlock / _NSConcreteGlobalBlock)
#include "veriobjc_runtime.h"

int32_t apply_non_escaping_block(int32_t input) {
    int32_t (^scale_and_offset)(int32_t) = ^int32_t(int32_t x) {
        return (x * 4) + 3;
    };
    return scale_and_offset(input);
}
