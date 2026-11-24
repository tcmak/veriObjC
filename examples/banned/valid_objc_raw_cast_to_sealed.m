// Valid stock Objective-C/C: casting a raw byte buffer to an Objective-C class pointer.
// Rejected by VeriObjC (Task 6.0.10): @sealed / @final instances require an 8-byte
// veriobjc_sealed_header prefix initialized by veriobjc_alloc_sealed.
#include "veriobjc_runtime.h"
#include "rt/dispatch.h"

__attribute__((objc_root_class))
__attribute__((annotate("objc_sealed")))
@interface RawCastSealedStage {
@public
    int32_t offset;
}
@end

@implementation RawCastSealedStage
@end

int32_t valid_objc_raw_buffer_cast_to_sealed(void) {
    _Alignas(8) uint8_t raw_storage[32] = {0};
    RawCastSealedStage *forged = (RawCastSealedStage *)raw_storage;
    return forged->offset;
}
