// Negative plugin test (Task 4.4): 32-bit signed overflow violating @post under real Z3 QF_BV
#include "veriobjc_runtime.h"
#include "vstd/contracts.h"

// Under unbounded math, price + margin > price when margin >= 1.
// Under exact C 32-bit signed two's-complement semantics (QF_BV), price = 2147483647, margin = 1
// wraps around to -2147483648 <= price, so Z3 finds a concrete counterexample!
vcontract_pre("price >= 0 && margin >= 1 && margin <= 100")
vcontract_post("result > price")
veriobjc_export veriobjc_pure int32_t unchecked_add_margin_i32(int32_t price, int32_t margin) {
    return price + margin;
}
