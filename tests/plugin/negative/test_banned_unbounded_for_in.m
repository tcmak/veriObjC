// Negative test: range-based for-in loop over a container without a compile-time bounded array field is banned
#include "veriobjc_runtime.h"

__attribute__((objc_root_class))
__attribute__((objc_sealed))
__attribute__((objc_final))
@interface SampleItem {
@public
    int32_t value;
}
@end

@implementation SampleItem
@end

__attribute__((objc_root_class))
__attribute__((objc_sealed))
__attribute__((objc_final))
@interface UnboundedContainer {
@public
    SampleItem **dynamic_items; // Not a compile-time fixed array!
    uint32_t count;
}
@end

@implementation UnboundedContainer
@end

int32_t sum_unbounded(UnboundedContainer *c) {
    int32_t total = 0;
    for (SampleItem *item in c) {
        total += item->value;
    }
    return total;
}
