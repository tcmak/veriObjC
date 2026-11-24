// Negative plugin test (Task 6.0.9a):
// Indirect calls (function pointers / blocks) directly or transitively reachable from an
// objc_hw_kernel hide arbitrary runtime multiplication/division and MUST fail closed.
#include "veriobjc_runtime.h"

typedef int32_t (*ternary_op_fn_t)(int32_t, int32_t);

static int32_t hidden_mul_impl(int32_t a, int32_t b) {
    return a * b;
}

static int32_t helper_dispatch_indirect(ternary_op_fn_t op, int32_t a, int32_t b) {
    return op(a, b);
}

veriobjc_hw_kernel
int32_t illegal_hw_kernel_direct_fnptr(ternary_op_fn_t fn, int32_t x, int32_t y) {
    return fn(x, y);
}

veriobjc_hw_kernel
int32_t illegal_hw_kernel_transitive_fnptr(int32_t x, int32_t y) {
    ternary_op_fn_t fn = hidden_mul_impl;
    return helper_dispatch_indirect(fn, x, y);
}
