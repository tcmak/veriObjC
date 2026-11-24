// Valid stock Objective-C/C: calling an unannotated external C99 function declared in
// an -isystem header from a latency-critical function.
// Rejected by VeriObjC (Task 6.0.6): unannotated extern C functions default to @cold
// even when imported via -isystem and cannot be called inside @nolatency code.
#include "veriobjc_runtime.h"
#include <synth_c99_frame.h>

__attribute__((annotate("objc_nolatency")))
uint32_t valid_objc_call_isystem_extern_in_nolatency(const uint8_t *buf, size_t len) {
    return synth_c99_external_audit_crc(buf, len);
}
