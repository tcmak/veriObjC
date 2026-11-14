// EXPECTED_DIAGNOSTIC: VeriObjC Violation: Invalid vgeneric type argument
#include "veriobjc_runtime.h"
#include "vstd/vector.h"

// Missing __attribute__((objc_sealed)) on subclass passed to VVector<T>
__attribute__((objc_root_class))
@interface UnsealedLotItem {
@public
    int32_t qty;
}
@end

@implementation UnsealedLotItem
@end

void use_unsealed_vvector_arg(VVector<UnsealedLotItem *> *bad_vec) {
    (void)bad_vec;
}
