// EXPECTED_DIAGNOSTIC: VeriObjC Violation: vvector push exceeds static capacity
// Valid Objective-C runtime bounds check, banned in VeriObjC when exceeding static VVector<T> capacity.
#include "veriobjc_runtime.h"
#include "vstd/vector.h"

__attribute__((annotate("objc_sealed")))
__attribute__((annotate("objc_final")))
@interface LotElem : VVectorItem {
@public
    int32_t qty;
}
@end

@implementation LotElem
@end

void overflow_vvector(VVector<LotElem *> *vec, LotElem *item) {
    if (!vec || !item) return;
    [vec initVec];
    [vec push:item];
    [vec push:item];
    [vec push:item];
    [vec push:item];
    [vec push:item];
}
