#ifndef VERIOBJC_RT_LATENCY_GATE_H
#define VERIOBJC_RT_LATENCY_GATE_H

#include "rt/bench_harness.h"

#ifdef __cplusplus
extern "C" {
#endif

enum rt_latency_gate_status {
    RT_LATENCY_GATE_OK = 0,
    RT_LATENCY_GATE_INVALID_ARG = 1,
    RT_LATENCY_GATE_M6_INVALID = 2,
    RT_LATENCY_GATE_BENCH_MISMATCH = 3,
    RT_LATENCY_GATE_P50_EXCEEDED = 4,
    RT_LATENCY_GATE_P99_EXCEEDED = 5,
    RT_LATENCY_GATE_P999_EXCEEDED = 6,
    RT_LATENCY_GATE_TEARDOWN_EXCEEDED = 7
};

struct rt_latency_budget {
    const char *bench_id;
    uint64_t max_p50_cycles;
    uint64_t max_p99_cycles;
    uint64_t max_p999_cycles;
    uint64_t max_teardown_cycles;
    uint64_t noise_band_cycles;
};

struct rt_latency_gate_report {
    enum rt_latency_gate_status status;
    const char *offending_bench_id;
    const char *offending_commit_id;
    uint64_t measured_cycles;
    uint64_t ceiling_cycles;
    char diagnostic[256];
};

static inline enum rt_latency_gate_status rt_latency_gate_check(
    const struct rt_latency_budget *budget,
    const struct rt_bench_m6_record *measured,
    const char *commit_id,
    struct rt_latency_gate_report *out_report) {
    (void)budget;
    (void)measured;
    (void)commit_id;
    (void)out_report;
    return RT_LATENCY_GATE_INVALID_ARG;
}

#ifdef __cplusplus
}
#endif

#endif /* VERIOBJC_RT_LATENCY_GATE_H */
