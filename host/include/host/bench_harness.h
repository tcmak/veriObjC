#ifndef VERIOBJC_HOST_BENCH_HARNESS_H
#define VERIOBJC_HOST_BENCH_HARNESS_H

#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

enum rt_bench_kernel_tier {
    RT_BENCH_TIER_CORE = 0,
    RT_BENCH_TIER_GROUPED = 1,
    RT_BENCH_TIER_EXTENDED = 2
};

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
    enum rt_bench_kernel_tier kernel_tier;
    enum rt_bench_dsp_mode dsp_mode;
    struct rt_bench_stats stats;
};

static inline uint64_t rt_bench_read_cycles(void) {
#if defined(__x86_64__) || defined(__i386__)
    uint32_t lo = 0u;
    uint32_t hi = 0u;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32u) | (uint64_t)lo;
#else
    return __builtin_readcyclecounter();
#endif
}

static inline int rt_bench_compute_stats(
    uint64_t *samples,
    size_t count,
    uint64_t teardown_cycles,
    struct rt_bench_stats *out_stats) {
    if (!samples || count == 0u || !out_stats) {
        return 0;
    }

    for (size_t i = 1u; i < count; ++i) {
        uint64_t key = samples[i];
        size_t j = i;
        while (j > 0u && samples[j - 1u] > key) {
            samples[j] = samples[j - 1u];
            j--;
        }
        samples[j] = key;
    }

    size_t idx50 = ((count - 1u) * 50u) / 100u;
    size_t idx99 = ((count - 1u) * 99u + 99u) / 100u;
    size_t idx999 = ((count - 1u) * 999u + 999u) / 1000u;

    out_stats->min_cycles = samples[0];
    out_stats->p50_cycles = samples[idx50];
    out_stats->p99_cycles = samples[idx99];
    out_stats->p999_cycles = samples[idx999];
    out_stats->max_cycles = samples[count - 1u];
    out_stats->teardown_cycles = teardown_cycles;
    return 1;
}

static inline int rt_bench_validate_m6_record(const struct rt_bench_m6_record *rec) {
    if (!rec || !rec->bench_id || rec->bench_id[0] == '\0') {
        return 0;
    }
    if (rec->shape_m == 0u || rec->shape_n == 0u || rec->shape_k == 0u) {
        return 0;
    }
    if (!rec->cited_baseline || rec->cited_baseline[0] == '\0') {
        return 0;
    }
    if (rec->dollar_norm_cents == 0u) {
        return 0;
    }
    if (rec->kernel_tier != RT_BENCH_TIER_CORE &&
        rec->kernel_tier != RT_BENCH_TIER_GROUPED &&
        rec->kernel_tier != RT_BENCH_TIER_EXTENDED) {
        return 0;
    }
    if (rec->dsp_mode != RT_BENCH_DSP_ZERO && rec->dsp_mode != RT_BENCH_DSP_ENABLED) {
        return 0;
    }
    if (rec->stats.p50_cycles == 0u || rec->stats.p99_cycles < rec->stats.p50_cycles) {
        return 0;
    }
    return 1;
}

static inline size_t rt_bench_append_str(char *dst, size_t pos, const char *s) {
    while (*s != '\0') {
        dst[pos++] = *s++;
    }
    return pos;
}

static inline size_t rt_bench_append_u64(char *dst, size_t pos, uint64_t val) {
    char rev[24];
    size_t n = 0u;
    uint64_t v = val;
    do {
        rev[n++] = (char)('0' + (v % 10u));
        v /= 10u;
    } while (v > 0u);
    while (n > 0u) {
        dst[pos++] = rev[--n];
    }
    return pos;
}

static inline int rt_bench_format_m6_line(
    const struct rt_bench_m6_record *rec,
    char *out_buf,
    size_t out_cap) {
    if (!out_buf || out_cap < 160u || !rt_bench_validate_m6_record(rec)) {
        return 0;
    }
    const char *tier_str =
        (rec->kernel_tier == RT_BENCH_TIER_EXTENDED)
            ? "extended"
            : ((rec->kernel_tier == RT_BENCH_TIER_GROUPED) ? "grouped" : "core");
    const char *dsp_str =
        (rec->dsp_mode == RT_BENCH_DSP_ENABLED) ? "dsp-enabled" : "zero-dsp";

    size_t p = 0u;
    p = rt_bench_append_str(out_buf, p, "bench_id=");
    p = rt_bench_append_str(out_buf, p, rec->bench_id);
    p = rt_bench_append_str(out_buf, p, " shape=");
    p = rt_bench_append_u64(out_buf, p, rec->shape_m);
    p = rt_bench_append_str(out_buf, p, "x");
    p = rt_bench_append_u64(out_buf, p, rec->shape_n);
    p = rt_bench_append_str(out_buf, p, "x");
    p = rt_bench_append_u64(out_buf, p, rec->shape_k);
    p = rt_bench_append_str(out_buf, p, " baseline=");
    p = rt_bench_append_str(out_buf, p, rec->cited_baseline);
    p = rt_bench_append_str(out_buf, p, " dollar_cents=");
    p = rt_bench_append_u64(out_buf, p, rec->dollar_norm_cents);
    p = rt_bench_append_str(out_buf, p, " kernel_tier=");
    p = rt_bench_append_str(out_buf, p, tier_str);
    p = rt_bench_append_str(out_buf, p, " dsp_mode=");
    p = rt_bench_append_str(out_buf, p, dsp_str);
    p = rt_bench_append_str(out_buf, p, " p50=");
    p = rt_bench_append_u64(out_buf, p, rec->stats.p50_cycles);
    p = rt_bench_append_str(out_buf, p, " p99=");
    p = rt_bench_append_u64(out_buf, p, rec->stats.p99_cycles);
    p = rt_bench_append_str(out_buf, p, " p999=");
    p = rt_bench_append_u64(out_buf, p, rec->stats.p999_cycles);
    p = rt_bench_append_str(out_buf, p, " teardown=");
    p = rt_bench_append_u64(out_buf, p, rec->stats.teardown_cycles);
    out_buf[p] = '\0';
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* VERIOBJC_HOST_BENCH_HARNESS_H */
