// test_valid_clock_domains.m — Task 7a.1 Positive Plugin Test
// Valid VeriObjC: typed clock domain operations (vticks, vns) with
// same-domain arithmetic and calibration.  Must compile cleanly
// under veriobjc-check with zero warnings.

#include "veriobjc_runtime.h"
#include <sys0/time.h>

// Forward-declare a root class for the plugin
__attribute__((objc_root_class, objc_sealed))
@interface ClockDomainDemo
- (uint64_t)measureLatency;
@end

@implementation ClockDomainDemo
- (uint64_t)measureLatency {
    // --- Same-domain vticks arithmetic (legal) ---
    vticks t1 = { .raw = 1000ULL };
    vticks t2 = { .raw = 2000ULL };
    vticks delta = vticks_sub(t2, t1);

    // --- Calibration and conversion (legal) ---
    struct sys0_clock_cal cal;
    sys0_clock_cal_init(&cal, 3000000000ULL, t1);

    vns elapsed = sys0_ticks_to_ns(&cal, delta);
    vns rounded = sys0_ticks_to_ns_round(&cal, delta);

    // --- Same-domain vns arithmetic (legal) ---
    vns total = vns_add(elapsed, rounded);
    (void)total;

    // --- Comparison within same domain (legal) ---
    if (vticks_lt(t1, t2)) {
        return elapsed.raw;
    }

    // --- ns to ticks round-trip (legal) ---
    vticks back = sys0_ns_to_ticks(&cal, elapsed);
    (void)back;

    return 0u;
}
@end
