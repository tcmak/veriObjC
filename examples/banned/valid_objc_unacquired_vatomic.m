// EXPECTED_DIAGNOSTIC: VeriObjC Violation: Direct vatomic payload access without linear acquire guard
// Valid C/Objective-C struct member access, banned in VeriObjC without a scoped vatomic_acquire linear guard.
#include "veriobjc_runtime.h"
#include "vstd/atomic.h"

vatomic_def(ProtectedSeq, int32_t);

int32_t read_atomic_without_acquire(ProtectedSeq *seq) {
    return seq->payload_DoNotAccessDirectly;
}
