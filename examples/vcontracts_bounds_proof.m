// examples/vcontracts_bounds_proof.m
// Demonstrates Task 3.14 (<vstd/contracts.h>):
//   - Pure (@pure) contract predicates and @pre / @post SMT boundary equations
#include "veriobjc_runtime.h"
#include "vstd/contracts.h"

vcontract_pre("price >= 1 && price <= 100000")
vcontract_post("result >= 0")
veriobjc_export veriobjc_pure int64_t clamp_limit_price(int64_t price) {
    if (!vcontract_in_range_i64(price, 1, 100000)) {
        return 0;
    }
    return price;
}

#ifdef VERIOBJC_EXAMPLE_MAIN
int main(void) {
    return (clamp_limit_price(500) == 500 && clamp_limit_price(-5) == 0) ? 0 : 1;
}
#endif
