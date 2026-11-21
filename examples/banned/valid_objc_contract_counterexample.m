// Valid stock Objective-C: off-by-one ring-buffer index bound violating @post(result < 64).
// Compiles cleanly under plain Clang 23 (-Werror), but fails under VeriObjC --smt-verify
// with Z3_SAT_COUNTEREXAMPLE and concrete witness { slot_idx = 64, result = 64 }.
#include "veriobjc_runtime.h"
#include "vstd/contracts.h"

vcontract_pre("slot_idx >= 0 && slot_idx <= 64")
vcontract_post("result >= 0 && result < 64")
__attribute__((annotate("objc_export")))
__attribute__((annotate("objc_pure")))
int32_t buggy_ring_slot_clamp(int32_t slot_idx) {
    if (slot_idx < 0) {
        return 0;
    }
    // Off-by-one bug: permits slot_idx == 64 to pass through unclamped!
    if (slot_idx > 64) {
        return 63;
    }
    return slot_idx;
}
