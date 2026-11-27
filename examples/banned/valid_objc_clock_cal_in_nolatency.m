// valid_objc_clock_cal_in_nolatency.m — Task 7a.1 Valid-ObjC Rejection Corpus
//
// This file is VALID stock Objective-C: it compiles cleanly under stock
// Clang 23 -Werror. It demonstrates calling a @cold calibration function
// from a @nolatency context — the kind of effect violation the VeriObjC
// plugin's existing Task 2.8.1 effect system catches.
//
// Under the VeriObjC plugin, calling a @cold function from a @nolatency
// context is rejected because @cold operations are quarantined to boot.
//
// Expected: Stock Clang 23 PASS
//           VeriObjC Plugin FAIL (@cold function in @nolatency context)

#include "veriobjc_runtime.h"
#include <sys0/time.h>

// A @cold calibration function (boot-time only)
__attribute__((annotate("objc_cold")))
static void do_calibrate(struct sys0_clock_cal *cal) {
    sys0_clock_cal_init(cal, 3000000000ULL, (vticks){ .raw = 0ULL });
}

// ERROR: Calling a @cold function from a @nolatency context
__attribute__((annotate("objc_nolatency")))
static uint64_t hot_path_illegal_calibrate(void) {
    struct sys0_clock_cal cal;
    do_calibrate(&cal);  // VeriObjC: @cold function in @nolatency context
    vticks now = { .raw = 42ULL };
    vns elapsed = sys0_ticks_to_ns(&cal, now);
    return elapsed.raw;
}

int main(void) {
    return (int)hot_path_illegal_calibrate();
}
