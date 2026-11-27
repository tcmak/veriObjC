#pragma once
#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <sys0/time.h> — Typed Clock Domains & Boot Calibration (Task 7a.1)
// Layer: sys0 (depends on: rt ONLY — no vstd, hw, or host)
//
// Design:
//   vticks — raw hardware cycle counter ticks (rdtsc / CNTVCT_EL0).
//   vns    — nanoseconds (calibrated from vticks via a pinned multiplier).
//
//   Both are vstrong_def nominal types (macro from rt/veriobjc_runtime.h):
//   cross-domain arithmetic (vticks + vns) is a compile error via type
//   incompatibility.  Higher-level comparison macros (vstrong_eq, vstrong_lt)
//   live in <vstd/strong.h>; this header provides domain-specific inline
//   functions using the raw struct field directly, respecting the sys0
//   layer rule (sys0 → rt only, never vstd).
//
//   Calibration is a one-time @cold operation that pins the cycles→nanoseconds
//   conversion factor at boot.  The calibration record is read-only after
//   initialization.
//
// Virtual Clock Rule (7a):
//   All unit tests run on an injected virtual tick source instead of wall time.
//   sys0_rdtsc appears only behind calibration or in host bench harnesses.
//
// Platform Contract:
//   x86_64:  rdtsc frequency via calibration; invariant TSC assumed (modern
//            CPUs since Nehalem); drift ≤ 1 ns/s on invariant-TSC cores.
//   aarch64: CNTVCT_EL0 at CNTFRQ_EL0 frequency; drift per SoC spec.
//   other:   returns 0; calibration produces a degenerate 0-frequency record.
// ---------------------------------------------------------------------------

// --- Strong Types (vticks, vns) -------------------------------------------
// vstrong_def is in rt/veriobjc_runtime.h: typedef struct { T raw; } Name;

vstrong_def(vticks, uint64_t);
vstrong_def(vns,    uint64_t);

// vticks arithmetic (same-domain only)
static inline vticks vticks_add(vticks a, vticks b) {
    return (vticks){ .raw = a.raw + b.raw };
}

static inline vticks vticks_sub(vticks a, vticks b) {
    return (vticks){ .raw = a.raw - b.raw };
}

static inline bool vticks_lt(vticks a, vticks b) {
    return a.raw < b.raw;
}

static inline bool vticks_eq(vticks a, vticks b) {
    return a.raw == b.raw;
}

static inline bool vticks_le(vticks a, vticks b) {
    return a.raw <= b.raw;
}

// vns arithmetic (same-domain only)
static inline vns vns_add(vns a, vns b) {
    return (vns){ .raw = a.raw + b.raw };
}

static inline vns vns_sub(vns a, vns b) {
    return (vns){ .raw = a.raw - b.raw };
}

static inline bool vns_lt(vns a, vns b) {
    return a.raw < b.raw;
}

static inline bool vns_eq(vns a, vns b) {
    return a.raw == b.raw;
}

static inline bool vns_le(vns a, vns b) {
    return a.raw <= b.raw;
}

// --- Hardware Cycle Counter -----------------------------------------------

static inline uint64_t sys0_rdtsc(void) {
#if defined(__x86_64__) || defined(__i386__)
    uint32_t lo = 0u;
    uint32_t hi = 0u;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32u) | (uint64_t)lo;
#elif defined(__aarch64__)
    uint64_t val = 0ULL;
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(val));
    return val;
#else
    return 0ULL;
#endif
}

static inline void sys0_lfence(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ __volatile__("lfence" ::: "memory");
#else
    __sync_synchronize();
#endif
}

// Typed wrapper returning vticks
static inline vticks sys0_read_ticks(void) {
    sys0_lfence();
    return (vticks){ .raw = sys0_rdtsc() };
}

// --- Calibration Record (read-only after @cold boot init) -----------------

// The calibration record pins the conversion factor from ticks to nanoseconds.
// After initialization it is treated as immutable.
//
// Conversion formula:
//   ns = (ticks * numer) / denom
//
// For a 3 GHz TSC: numer=1, denom=3  → 1 tick = 0.333 ns
// For a 1 GHz counter: numer=1, denom=1  → 1 tick = 1 ns
//
// Integer division: results are truncated (floor). For round-half-up,
// use sys0_ticks_to_ns_round().
//
// A degenerate record (frequency_hz == 0) indicates calibration failure;
// all conversions return 0 ticks / 0 ns.

struct sys0_clock_cal {
    uint64_t frequency_hz;  // Tick frequency (e.g. 3000000000 for 3 GHz TSC)
    uint64_t numer;         // Numerator of the ns/tick ratio
    uint64_t denom;         // Denominator of the ns/tick ratio (never 0 after valid init)
    vticks   epoch;         // Tick value at calibration time (the reference origin)
};

// --- Virtual Tick Source (for deterministic testing) -----------------------

// A virtual tick source replaces sys0_rdtsc for deterministic tests.
// Tests inject a tick source; production code uses sys0_read_ticks().
typedef vticks (*sys0_tick_source_fn)(void *ctx);

struct sys0_virtual_clock {
    sys0_tick_source_fn read_fn;   // User-supplied tick reader
    void               *ctx;      // Opaque context for the reader
};

static inline vticks sys0_virtual_clock_read(const struct sys0_virtual_clock *vc) {
    if (!vc || !vc->read_fn) {
        return (vticks){ .raw = 0ULL };
    }
    return vc->read_fn(vc->ctx);
}

// --- Calibration API (@cold) ----------------------------------------------

// Initialize a calibration record from a known frequency.
// This is a @cold boot-time operation.
//
// frequency_hz: the tick counter frequency in Hz (e.g. 3000000000 for 3 GHz).
// epoch_ticks:  the tick reading at calibration time.
//
// Returns false if frequency_hz == 0 (degenerate/failed calibration).
static inline bool sys0_clock_cal_init(
    struct sys0_clock_cal *cal,
    uint64_t frequency_hz,
    vticks epoch_ticks
) {
    if (!cal) {
        return false;
    }
    if (frequency_hz == 0ULL) {
        cal->frequency_hz = 0ULL;
        cal->numer = 0ULL;
        cal->denom = 1ULL;  // Never 0
        cal->epoch = (vticks){ .raw = 0ULL };
        return false;
    }

    cal->frequency_hz = frequency_hz;
    cal->epoch = epoch_ticks;

    // Compute numer/denom for  ns = ticks * (1e9 / frequency_hz)
    // We store the reduced fraction: numer = 1000000000 / gcd,
    // denom = frequency_hz / gcd.

    // GCD via binary algorithm (no division operator in hot path)
    uint64_t a = 1000000000ULL;
    uint64_t b = frequency_hz;

    // Count common trailing zeros
    uint32_t shift = 0u;
    while (((a | b) & 1ULL) == 0ULL) { a >>= 1u; b >>= 1u; ++shift; }
    while ((a & 1ULL) == 0ULL) { a >>= 1u; }
    do {
        while ((b & 1ULL) == 0ULL) { b >>= 1u; }
        if (a > b) { uint64_t t = a; a = b; b = t; }
        b -= a;
    } while (b != 0ULL);
    uint64_t gcd = a << shift;

    cal->numer = 1000000000ULL / gcd;
    cal->denom = frequency_hz / gcd;
    return true;
}

// Initialize a calibration record from a virtual clock source.
// Reads two tick samples separated by a known nanosecond interval
// to derive the frequency.  For deterministic tests, the virtual clock
// source returns scripted values.
static inline bool sys0_clock_cal_init_from_virtual(
    struct sys0_clock_cal *cal,
    const struct sys0_virtual_clock *vc,
    uint64_t known_interval_ns,
    vticks sample_start,
    vticks sample_end
) {
    if (!cal || !vc) {
        return false;
    }
    uint64_t delta = sample_end.raw - sample_start.raw;
    if (delta == 0ULL || known_interval_ns == 0ULL) {
        return sys0_clock_cal_init(cal, 0ULL, sample_start);
    }
    // frequency = delta_ticks / (interval_ns / 1e9) = delta_ticks * 1e9 / interval_ns
    // To avoid overflow on delta * 1e9, we compute carefully:
    uint64_t freq_hz = delta * (1000000000ULL / known_interval_ns);
    // Adjust for remainder: freq_hz += delta * (1e9 % interval_ns) / interval_ns
    uint64_t remainder_ns = 1000000000ULL % known_interval_ns;
    if (remainder_ns > 0ULL) {
        freq_hz += (delta * remainder_ns) / known_interval_ns;
    }
    return sys0_clock_cal_init(cal, freq_hz, sample_start);
}

// --- Conversion Functions -------------------------------------------------

// Convert a tick delta to nanoseconds using a calibration record.
// Returns 0 ns on degenerate (failed) calibration.
static inline vns sys0_ticks_to_ns(const struct sys0_clock_cal *cal, vticks delta) {
    if (!cal || cal->denom == 0ULL || cal->frequency_hz == 0ULL) {
        return (vns){ .raw = 0ULL };
    }
    // ns = raw * numer / denom  (truncating)
    return (vns){ .raw = (delta.raw * cal->numer) / cal->denom };
}

// Convert a tick delta to nanoseconds with round-half-up.
static inline vns sys0_ticks_to_ns_round(const struct sys0_clock_cal *cal, vticks delta) {
    if (!cal || cal->denom == 0ULL || cal->frequency_hz == 0ULL) {
        return (vns){ .raw = 0ULL };
    }
    uint64_t product = delta.raw * cal->numer;
    uint64_t half = cal->denom / 2ULL;
    return (vns){ .raw = (product + half) / cal->denom };
}

// Convert nanoseconds to ticks using a calibration record.
// Returns 0 ticks on degenerate calibration or zero numer.
static inline vticks sys0_ns_to_ticks(const struct sys0_clock_cal *cal, vns ns_val) {
    if (!cal || cal->numer == 0ULL || cal->frequency_hz == 0ULL) {
        return (vticks){ .raw = 0ULL };
    }
    // ticks = raw * denom / numer
    return (vticks){ .raw = (ns_val.raw * cal->denom) / cal->numer };
}

// Elapsed nanoseconds since calibration epoch.
static inline vns sys0_elapsed_ns(const struct sys0_clock_cal *cal, vticks now) {
    if (!cal || cal->frequency_hz == 0ULL) {
        return (vns){ .raw = 0ULL };
    }
    uint64_t delta = now.raw - cal->epoch.raw;
    return sys0_ticks_to_ns(cal, (vticks){ .raw = delta });
}

// Check if a calibration record is valid (non-degenerate).
static inline bool sys0_clock_cal_is_valid(const struct sys0_clock_cal *cal) {
    if (!cal) {
        return false;
    }
    return cal->frequency_hz != 0ULL;
}

#ifdef __cplusplus
}
#endif
