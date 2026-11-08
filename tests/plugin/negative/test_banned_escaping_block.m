// Negative test: storing a function block into an escaping location (global/field/return) is banned
#include "veriobjc_runtime.h"

typedef int32_t (^TransformBlock)(int32_t);

static TransformBlock g_escaped_block;

void escape_block_to_global(void) {
    g_escaped_block = ^int32_t(int32_t x) {
        return x + 1;
    };
}
