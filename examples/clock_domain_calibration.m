// clock_domain_calibration.m — Task 7a.1 Positive Example
// Demonstrates typed clock domains (vticks, vns) with @cold boot calibration.
//
// This example:
//   1. Initializes a calibration record for a 3 GHz TSC.
//   2. Converts tick deltas to nanoseconds (both truncated and rounded).
//   3. Performs a round-trip conversion: ns → ticks → ns.
//   4. Uses a virtual tick source for deterministic operation.
//
// Key guarantees:
//   - vticks and vns are distinct types: cross-domain mixing is a compile error.
//   - Calibration is a @cold boot-time operation; the record is read-only after.
//   - All operations are freestanding: zero malloc, zero libc, zero OS calls.
//   - Virtual clock: tests are deterministic (no wall-clock reads).
//
// Build:
//   clang-23 -std=gnu23 -O2 -Werror -fblocks \
//            -Xclang -load -Xclang build/libVeriObjCPlugin.so \
//            -Xclang -add-plugin -Xclang veriobjc-check \
//            -I./rt/include -I./vstd/include -I./sys0/include \
//            -c examples/clock_domain_calibration.m \
//            -o build/clock_domain_calibration.o

#include "veriobjc_runtime.h"
#include <sys0/time.h>

// --- Virtual tick source (for deterministic example) ---

static uint64_t g_virtual_tick = 0ULL;

static vticks example_tick_reader(void *ctx) {
    (void)ctx;
    return (vticks){ .raw = g_virtual_tick };
}

// --- Boot calibration (@cold) ---

static struct sys0_clock_cal g_cal;

veriobjc_cold
static void boot_calibrate(void) {
    // Pin the calibration at "boot time" with a known 3 GHz frequency
    vticks epoch = { .raw = g_virtual_tick };
    sys0_clock_cal_init(&g_cal, 3000000000ULL, epoch);
}

// --- Steady-state measurement (uses calibrated record, never touches the OS) ---

__attribute__((objc_root_class, objc_sealed))
@interface LatencyProbe
- (vns)measureDelta:(vticks)start end:(vticks)end;
- (vticks)nsToTicks:(vns)ns_val;
@end

@implementation LatencyProbe
- (vns)measureDelta:(vticks)start end:(vticks)end {
    vticks delta = vticks_sub(end, start);
    return sys0_ticks_to_ns(&g_cal, delta);
}

- (vticks)nsToTicks:(vns)ns_val {
    return sys0_ns_to_ticks(&g_cal, ns_val);
}
@end

#ifdef VERIOBJC_EXAMPLE_MAIN
#include <stdio.h>

int main(void) {
    // Boot phase: calibrate
    g_virtual_tick = 0ULL;
    boot_calibrate();

    // Set up virtual clock
    struct sys0_virtual_clock vc = {
        .read_fn = example_tick_reader,
        .ctx     = NULL
    };

    // Simulate: read tick at start and end of some operation
    g_virtual_tick = 1000ULL;
    vticks start = sys0_virtual_clock_read(&vc);

    g_virtual_tick = 1300ULL;  // 300 ticks later
    vticks end = sys0_virtual_clock_read(&vc);

    // Convert delta to nanoseconds
    vticks delta = vticks_sub(end, start);
    vns elapsed = sys0_ticks_to_ns(&g_cal, delta);
    vns rounded = sys0_ticks_to_ns_round(&g_cal, delta);

    printf("Delta: %llu ticks\n", (unsigned long long)delta.raw);
    printf("Elapsed (truncated): %llu ns\n", (unsigned long long)elapsed.raw);
    printf("Elapsed (rounded):   %llu ns\n", (unsigned long long)rounded.raw);

    // Round-trip: ns -> ticks -> ns
    vticks back = sys0_ns_to_ticks(&g_cal, elapsed);
    vns roundtrip = sys0_ticks_to_ns(&g_cal, back);
    printf("Round-trip: %llu ns -> %llu ticks -> %llu ns\n",
           (unsigned long long)elapsed.raw,
           (unsigned long long)back.raw,
           (unsigned long long)roundtrip.raw);

    printf("Clock domain calibration example passed.\n");
    return 0;
}
#endif
