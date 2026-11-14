// EXPECTED_DIAGNOSTIC: VeriObjC Violation: Invalid vgeneric type argument
// Valid Objective-C lightweight generic specialization, banned in VeriObjC when the type argument is not @sealed.
#include "veriobjc_runtime.h"
#include "vstd/vector.h"

@interface UnsealedLotItem : VVectorItem {
@public
    int32_t qty;
}
@end

@implementation UnsealedLotItem
@end

void use_unsealed_vvector_arg(VVector<UnsealedLotItem *> *bad_vec) {
    (void)bad_vec;
}
