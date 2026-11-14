// EXPECTED_DIAGNOSTIC: VeriObjC Violation: vvector push exceeds static capacity
#include "veriobjc_runtime.h"
#include "vstd/vector.h"

__attribute__((objc_sealed))
__attribute__((objc_final))
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
    // 5th push on a capacity-4 VVector<T> must fail at compile time
    [vec push:item];
}
