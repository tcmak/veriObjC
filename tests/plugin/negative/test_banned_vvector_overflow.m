// EXPECTED_DIAGNOSTIC: VeriObjC Violation: vvector push exceeds static capacity
#include "veriobjc_runtime.h"
#include "vstd/vector.h"

vvector_def(TinyVec2, int32_t, 2);

void overflow_vvector(void) {
    TinyVec2 vec;
    vvector_init(&vec);
    vvector_push(&vec, 10);
    vvector_push(&vec, 20);
    // 3rd push on a capacity-2 vvector must fail at compile time
    vvector_push(&vec, 30);
}
