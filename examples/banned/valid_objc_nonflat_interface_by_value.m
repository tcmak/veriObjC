// EXPECTED_DIAGNOSTIC: VeriObjC Violation: Interface 'UnsealedPacket' cannot be allocated or returned by value: only @flat @final @sealed interfaces support value semantics (VG-006)
// Valid stock Objective-C/C value carrier, banned in VeriObjC when the referenced @interface is not @flat @final @sealed (Task 3.3c / VG-006).
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

typedef vinterface_by_value(UnsealedPacket) UnsealedPacketByVal;

UnsealedPacketByVal build_unsealed_packet_by_value(void) {
    UnsealedPacketByVal pkt = {0u};
    return pkt;
}
