// EXPECTED_DIAGNOSTIC: VeriObjC Violation: vvector push exceeds static capacity 8 on 'vec8'
#include "veriobjc_runtime.h"
#include "vstd/vector.h"

VVector_decl(int32_t, 8);

void overflow_macro_vvector_cap8(void) {
    VVector(int32_t, 8) vec8;
    vvector_init(&vec8);
    vvector_push(&vec8, 1);
    vvector_push(&vec8, 2);
    vvector_push(&vec8, 3);
    vvector_push(&vec8, 4);
    vvector_push(&vec8, 5);
    vvector_push(&vec8, 6);
    vvector_push(&vec8, 7);
    vvector_push(&vec8, 8);
    // 9th push on capacity-8 VVector must fail at compile time
    vvector_push(&vec8, 9);
}
