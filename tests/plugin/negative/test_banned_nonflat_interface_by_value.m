// EXPECTED_DIAGNOSTIC: VeriObjC Violation: Interface 'UnsealedPacket' cannot be allocated or returned by value: only @flat @final @sealed interfaces support value semantics (VG-006)
#include "veriobjc_runtime.h"
#include "vstd/vector.h"

__attribute__((objc_root_class))
@interface UnsealedPacket {
@public
    uint32_t seq;
}
@end

@implementation UnsealedPacket
@end

UnsealedPacket build_unsealed_packet_by_value(void) {
    UnsealedPacket pkt;
    return pkt;
}
