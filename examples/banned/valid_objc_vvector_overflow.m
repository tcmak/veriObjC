// EXPECTED_DIAGNOSTIC: VeriObjC Violation: vvector push exceeds static capacity
// Valid C/Objective-C runtime bounds check, banned in VeriObjC when exceeding static vvector capacity.
#include "veriobjc_runtime.h"
#include "vstd/vector.h"

vvector_def(TinyVec2, int32_t, 2);

void overflow_vvector(void) {
    TinyVec2 vec;
    vvector_init(&vec);
    vvector_push(&vec, 10);
    vvector_push(&vec, 20);
    vvector_push(&vec, 30);
}
