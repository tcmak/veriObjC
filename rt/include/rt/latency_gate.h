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

static inline int rt_latency_str_eq(const char *a, const char *b) {
    size_t i = 0u;
    while (a[i] != '\0' && a[i] == b[i]) {
        ++i;
    }
    return a[i] == b[i];
}

static inline void rt_latency_format_diag(
    struct rt_latency_gate_report *rep,
    const char *bench_id,
    const char *commit_id,
    const char *metric,
    const char *verdict) {
    size_t pos = 0u;
    pos = rt_bench_append_str(rep->diagnostic, pos, "bench=");
    pos = rt_bench_append_str(rep->diagnostic, pos, bench_id);
    pos = rt_bench_append_str(rep->diagnostic, pos, " commit=");
    pos = rt_bench_append_str(rep->diagnostic, pos, commit_id);
    pos = rt_bench_append_str(rep->diagnostic, pos, " metric=");
    pos = rt_bench_append_str(rep->diagnostic, pos, metric);
    pos = rt_bench_append_str(rep->diagnostic, pos, " status=");
    pos = rt_bench_append_str(rep->diagnostic, pos, verdict);
    rep->diagnostic[pos] = '\0';
}

static inline enum rt_latency_gate_status rt_latency_gate_check(
    const struct rt_latency_budget *budget,
    const struct rt_bench_m6_record *measured,
    const char *commit_id,
    struct rt_latency_gate_report *out_report) {
    if (!budget || !measured || !commit_id || !out_report ||
        !budget->bench_id || commit_id[0] == '\0') {
        return RT_LATENCY_GATE_INVALID_ARG;
    }
    if (!rt_bench_validate_m6_record(measured)) {
        return RT_LATENCY_GATE_M6_INVALID;
    }
    if (!rt_latency_str_eq(budget->bench_id, measured->bench_id)) {
        return RT_LATENCY_GATE_BENCH_MISMATCH;
    }

    out_report->offending_bench_id = budget->bench_id;
    out_report->offending_commit_id = commit_id;

    uint64_t p50_ceil = budget->max_p50_cycles + budget->noise_band_cycles;
    if (measured->stats.p50_cycles > p50_ceil) {
        out_report->status = RT_LATENCY_GATE_P50_EXCEEDED;
        out_report->measured_cycles = measured->stats.p50_cycles;
        out_report->ceiling_cycles = p50_ceil;
        rt_latency_format_diag(out_report, budget->bench_id, commit_id, "p50", "FAIL");
        return RT_LATENCY_GATE_P50_EXCEEDED;
    }

    uint64_t p99_ceil = budget->max_p99_cycles + budget->noise_band_cycles;
    if (measured->stats.p99_cycles > p99_ceil) {
        out_report->status = RT_LATENCY_GATE_P99_EXCEEDED;
        out_report->measured_cycles = measured->stats.p99_cycles;
        out_report->ceiling_cycles = p99_ceil;
        rt_latency_format_diag(out_report, budget->bench_id, commit_id, "p99", "FAIL");
        return RT_LATENCY_GATE_P99_EXCEEDED;
    }

    uint64_t p999_ceil = budget->max_p999_cycles + budget->noise_band_cycles;
    if (measured->stats.p999_cycles > p999_ceil) {
        out_report->status = RT_LATENCY_GATE_P999_EXCEEDED;
        out_report->measured_cycles = measured->stats.p999_cycles;
        out_report->ceiling_cycles = p999_ceil;
        rt_latency_format_diag(out_report, budget->bench_id, commit_id, "p999", "FAIL");
        return RT_LATENCY_GATE_P999_EXCEEDED;
    }

    uint64_t td_ceil = budget->max_teardown_cycles + budget->noise_band_cycles;
    if (measured->stats.teardown_cycles > td_ceil) {
        out_report->status = RT_LATENCY_GATE_TEARDOWN_EXCEEDED;
        out_report->measured_cycles = measured->stats.teardown_cycles;
        out_report->ceiling_cycles = td_ceil;
        rt_latency_format_diag(out_report, budget->bench_id, commit_id, "teardown", "FAIL");
        return RT_LATENCY_GATE_TEARDOWN_EXCEEDED;
    }

    out_report->status = RT_LATENCY_GATE_OK;
    out_report->measured_cycles = measured->stats.p99_cycles;
    out_report->ceiling_cycles = p99_ceil;
    rt_latency_format_diag(out_report, budget->bench_id, commit_id, "all", "PASS");
    return RT_LATENCY_GATE_OK;
}

#ifdef __cplusplus
}
#endif

#endif /* VERIOBJC_RT_LATENCY_GATE_H */
