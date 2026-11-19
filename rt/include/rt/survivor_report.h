#ifndef VERIOBJC_RT_SURVIVOR_REPORT_H
#define VERIOBJC_RT_SURVIVOR_REPORT_H

#include "rt/bench_harness.h"

#ifdef __cplusplus
extern "C" {
#endif

enum rt_survivor_diff_status {
    RT_SURVIVOR_DIFF_OK = 0,
    RT_SURVIVOR_DIFF_INVALID_ARG = 1,
    RT_SURVIVOR_DIFF_NEW_ALLOC_EFFECT = 2,
    RT_SURVIVOR_DIFF_NEW_EFFECT_SITE = 3,
    RT_SURVIVOR_DIFF_NEW_UNPROVEN_BRANCH = 4,
    RT_SURVIVOR_DIFF_MONO_REGRESSION = 5,
    RT_SURVIVOR_DIFF_NONZERO_DSP = 6
};

struct rt_survivor_snapshot {
    const char *function_or_kernel;
    uint32_t effect_survivors;
    uint32_t alloc_survivors;
    uint32_t proven_branches;
    uint32_t hinted_branches;
    uint32_t unproven_branches;
    uint32_t mono_instantiations;
    uint32_t mono_code_bytes;
    uint32_t hw_luts;
    uint32_t hw_ffs;
    uint32_t hw_brams;
    uint32_t hw_dsp_blocks;
    vtpe_v1_profile_t vtpe_profile;
    enum rt_bench_dsp_mode dsp_mode;
};

struct rt_survivor_diff_result {
    enum rt_survivor_diff_status status;
    const char *offending_function;
    const char *reason;
};

static inline enum rt_survivor_diff_status rt_survivor_diff_audit(
    const struct rt_survivor_snapshot *baseline,
    const struct rt_survivor_snapshot *candidate,
    struct rt_survivor_diff_result *out_res) {
    (void)baseline;
    (void)candidate;
    (void)out_res;
    return RT_SURVIVOR_DIFF_INVALID_ARG;
}

#ifdef __cplusplus
}
#endif

#endif /* VERIOBJC_RT_SURVIVOR_REPORT_H */
