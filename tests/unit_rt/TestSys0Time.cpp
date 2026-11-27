#include "CppUTest/TestHarness.h"

extern "C" {
#include "veriobjc_runtime.h"
#include <sys0/time.h>
}

// ---------------------------------------------------------------------------
// Virtual tick source for deterministic testing (Rule 10: no wall-clock reads)
// ---------------------------------------------------------------------------

struct virtual_tick_state {
    uint64_t current_tick;
};

static vticks virtual_tick_reader(void *ctx) {
    struct virtual_tick_state *state = (struct virtual_tick_state *)ctx;
    return (vticks){ .raw = state->current_tick };
}

// ---------------------------------------------------------------------------
// TEST GROUP: Sys0TimeGroup — Task 7a.1 Boot Calibration & Typed Clock Domains
// ---------------------------------------------------------------------------

TEST_GROUP(Sys0TimeGroup) {};

// --- Strong Type Safety Tests ---
// vticks and vns are distinct types with same-domain-only arithmetic.
// Cross-domain mixing is caught at compile time by type incompatibility.

TEST(Sys0TimeGroup, VticksSameDomainArithmetic) {
    vticks a = { .raw = 100ULL };
    vticks b = { .raw = 50ULL };

    vticks sum = vticks_add(a, b);
    UNSIGNED_LONGS_EQUAL(150ULL, sum.raw);

    vticks diff = vticks_sub(a, b);
    UNSIGNED_LONGS_EQUAL(50ULL, diff.raw);

    CHECK_TRUE(vticks_lt(b, a));
    CHECK_FALSE(vticks_lt(a, b));
    CHECK_TRUE(vticks_eq(a, a));
    CHECK_FALSE(vticks_eq(a, b));
    CHECK_TRUE(vticks_le(b, a));
    CHECK_TRUE(vticks_le(a, a));
}

TEST(Sys0TimeGroup, VnsSameDomainArithmetic) {
    vns a = { .raw = 1000ULL };
    vns b = { .raw = 300ULL };

    vns sum = vns_add(a, b);
    UNSIGNED_LONGS_EQUAL(1300ULL, sum.raw);

    vns diff = vns_sub(a, b);
    UNSIGNED_LONGS_EQUAL(700ULL, diff.raw);

    CHECK_TRUE(vns_lt(b, a));
    CHECK_FALSE(vns_lt(a, b));
    CHECK_TRUE(vns_eq(a, a));
    CHECK_FALSE(vns_eq(a, b));
    CHECK_TRUE(vns_le(b, a));
    CHECK_TRUE(vns_le(a, a));
}

// --- Virtual Clock Tests ---

TEST(Sys0TimeGroup, VirtualClockRead) {
    struct virtual_tick_state state = { .current_tick = 42ULL };
    struct sys0_virtual_clock vc = {
        .read_fn = virtual_tick_reader,
        .ctx     = &state
    };

    vticks t = sys0_virtual_clock_read(&vc);
    UNSIGNED_LONGS_EQUAL(42ULL, t.raw);

    // Advance virtual time
    state.current_tick = 999ULL;
    t = sys0_virtual_clock_read(&vc);
    UNSIGNED_LONGS_EQUAL(999ULL, t.raw);
}

TEST(Sys0TimeGroup, VirtualClockNullSafety) {
    vticks t = sys0_virtual_clock_read(nullptr);
    UNSIGNED_LONGS_EQUAL(0ULL, t.raw);

    struct sys0_virtual_clock vc_no_fn = { .read_fn = nullptr, .ctx = nullptr };
    t = sys0_virtual_clock_read(&vc_no_fn);
    UNSIGNED_LONGS_EQUAL(0ULL, t.raw);
}

// --- Calibration Tests ---

TEST(Sys0TimeGroup, CalibrationInitFromKnownFrequency) {
    // 3 GHz TSC: 1 tick = 1/3 ns
    struct sys0_clock_cal cal;
    vticks epoch = { .raw = 1000ULL };
    bool ok = sys0_clock_cal_init(&cal, 3000000000ULL, epoch);

    CHECK_TRUE(ok);
    CHECK_TRUE(sys0_clock_cal_is_valid(&cal));
    UNSIGNED_LONGS_EQUAL(3000000000ULL, cal.frequency_hz);
    UNSIGNED_LONGS_EQUAL(1000ULL, cal.epoch.raw);

    // The reduced fraction for 1e9/3e9 = 1/3
    UNSIGNED_LONGS_EQUAL(1ULL, cal.numer);
    UNSIGNED_LONGS_EQUAL(3ULL, cal.denom);
}

TEST(Sys0TimeGroup, CalibrationInit1GHz) {
    // 1 GHz: 1 tick = 1 ns  → numer=1, denom=1
    struct sys0_clock_cal cal;
    bool ok = sys0_clock_cal_init(&cal, 1000000000ULL, (vticks){ .raw = 0ULL });
    CHECK_TRUE(ok);
    UNSIGNED_LONGS_EQUAL(1ULL, cal.numer);
    UNSIGNED_LONGS_EQUAL(1ULL, cal.denom);
}

TEST(Sys0TimeGroup, CalibrationInit2_4GHz) {
    // 2.4 GHz: 1e9/2.4e9 = 5/12
    struct sys0_clock_cal cal;
    bool ok = sys0_clock_cal_init(&cal, 2400000000ULL, (vticks){ .raw = 0ULL });
    CHECK_TRUE(ok);
    // gcd(1000000000, 2400000000) = 200000000
    // numer = 1000000000/200000000 = 5
    // denom = 2400000000/200000000 = 12
    UNSIGNED_LONGS_EQUAL(5ULL, cal.numer);
    UNSIGNED_LONGS_EQUAL(12ULL, cal.denom);
}

TEST(Sys0TimeGroup, CalibrationDegenerateZeroFrequency) {
    struct sys0_clock_cal cal;
    bool ok = sys0_clock_cal_init(&cal, 0ULL, (vticks){ .raw = 0ULL });
    CHECK_FALSE(ok);
    CHECK_FALSE(sys0_clock_cal_is_valid(&cal));
    UNSIGNED_LONGS_EQUAL(0ULL, cal.frequency_hz);
    // denom must never be 0
    CHECK_TRUE(cal.denom >= 1ULL);
}

TEST(Sys0TimeGroup, CalibrationNullPointer) {
    bool ok = sys0_clock_cal_init(nullptr, 3000000000ULL, (vticks){ .raw = 0ULL });
    CHECK_FALSE(ok);
}

// --- Conversion Tests ---

TEST(Sys0TimeGroup, TicksToNanoseconds3GHz) {
    struct sys0_clock_cal cal;
    sys0_clock_cal_init(&cal, 3000000000ULL, (vticks){ .raw = 0ULL });

    // 3 ticks at 3 GHz = 1 ns
    vns ns = sys0_ticks_to_ns(&cal, (vticks){ .raw = 3ULL });
    UNSIGNED_LONGS_EQUAL(1ULL, ns.raw);

    // 300 ticks at 3 GHz = 100 ns
    ns = sys0_ticks_to_ns(&cal, (vticks){ .raw = 300ULL });
    UNSIGNED_LONGS_EQUAL(100ULL, ns.raw);

    // 0 ticks = 0 ns
    ns = sys0_ticks_to_ns(&cal, (vticks){ .raw = 0ULL });
    UNSIGNED_LONGS_EQUAL(0ULL, ns.raw);
}

TEST(Sys0TimeGroup, TicksToNanoseconds1GHz) {
    struct sys0_clock_cal cal;
    sys0_clock_cal_init(&cal, 1000000000ULL, (vticks){ .raw = 0ULL });

    // 1 tick at 1 GHz = 1 ns
    vns ns = sys0_ticks_to_ns(&cal, (vticks){ .raw = 1ULL });
    UNSIGNED_LONGS_EQUAL(1ULL, ns.raw);

    // 1000 ticks = 1000 ns
    ns = sys0_ticks_to_ns(&cal, (vticks){ .raw = 1000ULL });
    UNSIGNED_LONGS_EQUAL(1000ULL, ns.raw);
}

TEST(Sys0TimeGroup, TicksToNanosecondsRoundHalfUp) {
    struct sys0_clock_cal cal;
    // 3 GHz: ratio is 1/3
    // 2 ticks = 0.666 ns → truncated = 0, rounded = 1
    sys0_clock_cal_init(&cal, 3000000000ULL, (vticks){ .raw = 0ULL });

    vns truncated = sys0_ticks_to_ns(&cal, (vticks){ .raw = 2ULL });
    vns rounded   = sys0_ticks_to_ns_round(&cal, (vticks){ .raw = 2ULL });
    UNSIGNED_LONGS_EQUAL(0ULL, truncated.raw);  // 2*1/3 = 0
    UNSIGNED_LONGS_EQUAL(1ULL, rounded.raw);    // (2*1 + 1)/3 = 1

    // 5 ticks = 1.666 ns → truncated = 1, rounded = 2
    truncated = sys0_ticks_to_ns(&cal, (vticks){ .raw = 5ULL });
    rounded   = sys0_ticks_to_ns_round(&cal, (vticks){ .raw = 5ULL });
    UNSIGNED_LONGS_EQUAL(1ULL, truncated.raw);  // 5*1/3 = 1
    UNSIGNED_LONGS_EQUAL(2ULL, rounded.raw);    // (5*1 + 1)/3 = 2
}

TEST(Sys0TimeGroup, NanosecondsToTicks) {
    struct sys0_clock_cal cal;
    sys0_clock_cal_init(&cal, 3000000000ULL, (vticks){ .raw = 0ULL });

    // 1 ns at 3 GHz = 3 ticks
    vticks t = sys0_ns_to_ticks(&cal, (vns){ .raw = 1ULL });
    UNSIGNED_LONGS_EQUAL(3ULL, t.raw);

    // 100 ns = 300 ticks
    t = sys0_ns_to_ticks(&cal, (vns){ .raw = 100ULL });
    UNSIGNED_LONGS_EQUAL(300ULL, t.raw);

    // Round-trip: 100 ns → ticks → ns = 100
    vns roundtrip = sys0_ticks_to_ns(&cal, t);
    UNSIGNED_LONGS_EQUAL(100ULL, roundtrip.raw);
}

TEST(Sys0TimeGroup, NanosecondsToTicksRoundTrip1GHz) {
    struct sys0_clock_cal cal;
    sys0_clock_cal_init(&cal, 1000000000ULL, (vticks){ .raw = 0ULL });

    // 1:1 ratio: perfect round-trip
    for (uint64_t ns_val = 0ULL; ns_val < 100ULL; ++ns_val) {
        vticks t = sys0_ns_to_ticks(&cal, (vns){ .raw = ns_val });
        UNSIGNED_LONGS_EQUAL(ns_val, t.raw);
        vns rt = sys0_ticks_to_ns(&cal, t);
        UNSIGNED_LONGS_EQUAL(ns_val, rt.raw);
    }
}

TEST(Sys0TimeGroup, ConversionDegenerateCalibration) {
    struct sys0_clock_cal cal;
    sys0_clock_cal_init(&cal, 0ULL, (vticks){ .raw = 0ULL });

    // All conversions return 0 on degenerate calibration
    vns ns = sys0_ticks_to_ns(&cal, (vticks){ .raw = 999ULL });
    UNSIGNED_LONGS_EQUAL(0ULL, ns.raw);

    ns = sys0_ticks_to_ns_round(&cal, (vticks){ .raw = 999ULL });
    UNSIGNED_LONGS_EQUAL(0ULL, ns.raw);

    vticks t = sys0_ns_to_ticks(&cal, (vns){ .raw = 999ULL });
    UNSIGNED_LONGS_EQUAL(0ULL, t.raw);
}

TEST(Sys0TimeGroup, ConversionNullCalibration) {
    vns ns = sys0_ticks_to_ns(nullptr, (vticks){ .raw = 100ULL });
    UNSIGNED_LONGS_EQUAL(0ULL, ns.raw);

    vticks t = sys0_ns_to_ticks(nullptr, (vns){ .raw = 100ULL });
    UNSIGNED_LONGS_EQUAL(0ULL, t.raw);
}

// --- Elapsed Since Epoch ---

TEST(Sys0TimeGroup, ElapsedNsSinceEpoch) {
    struct sys0_clock_cal cal;
    vticks epoch = { .raw = 1000ULL };
    sys0_clock_cal_init(&cal, 1000000000ULL, epoch);

    // At epoch: elapsed = 0
    vns e = sys0_elapsed_ns(&cal, epoch);
    UNSIGNED_LONGS_EQUAL(0ULL, e.raw);

    // 500 ticks after epoch at 1 GHz = 500 ns
    vticks now = { .raw = 1500ULL };
    e = sys0_elapsed_ns(&cal, now);
    UNSIGNED_LONGS_EQUAL(500ULL, e.raw);
}

TEST(Sys0TimeGroup, ElapsedNsDegenerateCalibration) {
    struct sys0_clock_cal cal;
    sys0_clock_cal_init(&cal, 0ULL, (vticks){ .raw = 0ULL });
    vns e = sys0_elapsed_ns(&cal, (vticks){ .raw = 999ULL });
    UNSIGNED_LONGS_EQUAL(0ULL, e.raw);
}

// --- Virtual Clock Calibration ---

TEST(Sys0TimeGroup, CalibrationFromVirtualClock3GHz) {
    // Simulate a 3 GHz clock: 3000 ticks in 1000 ns (1 µs)
    struct virtual_tick_state state = { .current_tick = 0ULL };
    struct sys0_virtual_clock vc = {
        .read_fn = virtual_tick_reader,
        .ctx     = &state
    };

    vticks start = { .raw = 0ULL };
    vticks end   = { .raw = 3000ULL };

    struct sys0_clock_cal cal;
    bool ok = sys0_clock_cal_init_from_virtual(&cal, &vc, 1000ULL, start, end);
    CHECK_TRUE(ok);
    CHECK_TRUE(sys0_clock_cal_is_valid(&cal));
    UNSIGNED_LONGS_EQUAL(3000000000ULL, cal.frequency_hz);

    // Verify conversions work: 3 ticks = 1 ns
    vns ns = sys0_ticks_to_ns(&cal, (vticks){ .raw = 3ULL });
    UNSIGNED_LONGS_EQUAL(1ULL, ns.raw);
}

TEST(Sys0TimeGroup, CalibrationFromVirtualClockZeroDelta) {
    struct virtual_tick_state state = { .current_tick = 0ULL };
    struct sys0_virtual_clock vc = {
        .read_fn = virtual_tick_reader,
        .ctx     = &state
    };

    vticks start = { .raw = 100ULL };
    vticks end   = { .raw = 100ULL };  // zero delta

    struct sys0_clock_cal cal;
    bool ok = sys0_clock_cal_init_from_virtual(&cal, &vc, 1000ULL, start, end);
    CHECK_FALSE(ok);
    CHECK_FALSE(sys0_clock_cal_is_valid(&cal));
}

TEST(Sys0TimeGroup, CalibrationFromVirtualClockNullVC) {
    struct sys0_clock_cal cal;
    vticks start = { .raw = 0ULL };
    vticks end   = { .raw = 3000ULL };
    bool ok = sys0_clock_cal_init_from_virtual(&cal, nullptr, 1000ULL, start, end);
    CHECK_FALSE(ok);
}

// --- sys0_read_ticks (typed wrapper) ---

TEST(Sys0TimeGroup, ReadTicksReturnsVticks) {
    // Just verify the typed wrapper compiles and returns a vticks
    vticks t = sys0_read_ticks();
    (void)t;
    CHECK_TRUE(true);  // Compilation is the test
}

// --- Cross-domain type safety (compile-time) ---
// The following test verifies that vticks and vns are structurally distinct.
// If someone tried to pass a vticks where a vns is expected, the C compiler
// would reject it. We verify they are distinct sizeof-compatible but
// type-incompatible structures.

TEST(Sys0TimeGroup, TypeSafetyDistinctStructs) {
    // Both are 8 bytes (uint64_t wrapper)
    UNSIGNED_LONGS_EQUAL(sizeof(uint64_t), sizeof(vticks));
    UNSIGNED_LONGS_EQUAL(sizeof(uint64_t), sizeof(vns));

    // vticks_add accepts vticks, vns_add accepts vns — they are different types.
    // This test exists to document the design; the compile-time check is
    // that the functions have different parameter types.
    vticks ta = { .raw = 1ULL };
    vticks tb = { .raw = 2ULL };
    vticks tc = vticks_add(ta, tb);
    UNSIGNED_LONGS_EQUAL(3ULL, tc.raw);

    vns na = { .raw = 1ULL };
    vns nb = { .raw = 2ULL };
    vns nc = vns_add(na, nb);
    UNSIGNED_LONGS_EQUAL(3ULL, nc.raw);
}
