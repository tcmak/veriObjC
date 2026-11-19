#ifndef VERIOBJC_RT_BENCH_HARNESS_H
#define VERIOBJC_RT_BENCH_HARNESS_H

#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

enum rt_bench_dsp_mode {
    RT_BENCH_DSP_ZERO = 0,
    RT_BENCH_DSP_ENABLED = 1
};

struct rt_bench_stats {
    uint64_t min_cycles;
    uint64_t p50_cycles;
    uint64_t p99_cycles;
    uint64_t p999_cycles;
    uint64_t max_cycles;
    uint64_t teardown_cycles;
};

struct rt_bench_m6_record {
    const char *bench_id;
    uint16_t shape_m;
    uint16_t shape_n;
    uint16_t shape_k;
    const char *cited_baseline;
    uint32_t dollar_norm_cents;
    vtpe_v1_profile_t vtpe_profile;
    enum rt_bench_dsp_mode dsp_mode;
    struct rt_bench_stats stats;
};

static inline uint64_t rt_bench_read_cycles(void) {
    return 0u;
}

static inline int rt_bench_compute_stats(
    uint64_t *samples,
    size_t count,
    uint64_t teardown_cycles,
    struct rt_bench_stats *out_stats) {
    (void)samples;
    (void)count;
    (void)teardown_cycles;
    (void)out_stats;
    return 0;
}

static inline int rt_bench_validate_m6_record(const struct rt_bench_m6_record *rec) {
    (void)rec;
    return 0;
}

static inline int rt_bench_format_m6_line(
    const struct rt_bench_m6_record *rec,
    char *out_buf,
    size_t out_cap) {
    (void)rec;
    (void)out_buf;
    (void)out_cap;
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* VERIOBJC_RT_BENCH_HARNESS_H */
