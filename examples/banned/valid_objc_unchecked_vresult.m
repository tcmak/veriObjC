// Valid stock Objective-C/C: reading a union/struct result field without checking is_ok.
// Rejected by VeriObjC (Task 3.7): vresult.value requires a branch-proven .is_ok check.
#include <vstd/result.h>

vresult_def(WireDecodeResult, uint32_t, int32_t);

uint32_t valid_objc_unchecked_result_read(WireDecodeResult r) {
    return r.value;
}
