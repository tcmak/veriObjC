// Negative test: `auto` / `vauto` type inference in function or method parameter position is banned
#include "veriobjc_runtime.h"

int32_t bad_inferred_param(vauto x) {
    (void)x;
    return 0;
}
