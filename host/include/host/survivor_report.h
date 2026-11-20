#ifndef VERIOBJC_HOST_SURVIVOR_REPORT_H
#define VERIOBJC_HOST_SURVIVOR_REPORT_H

#include "host/bench_harness.h"

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
    ttape_v1_profile_t ttape_profile;
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
    if (!baseline || !candidate || !out_res ||
        !baseline->function_or_kernel || !candidate->function_or_kernel) {
        return RT_SURVIVOR_DIFF_INVALID_ARG;
    }
    if (candidate->ttape_profile != TTAPE_PROFILE_CORE &&
        candidate->ttape_profile != TTAPE_PROFILE_GROUPED &&
        candidate->ttape_profile != TTAPE_PROFILE_EXTENDED) {
        return RT_SURVIVOR_DIFF_INVALID_ARG;
    }

    out_res->offending_function = candidate->function_or_kernel;
    if (candidate->alloc_survivors > baseline->alloc_survivors) {
        out_res->status = RT_SURVIVOR_DIFF_NEW_ALLOC_EFFECT;
        out_res->reason = "added @alloc call site in certified function";
        return RT_SURVIVOR_DIFF_NEW_ALLOC_EFFECT;
    }
    if (candidate->effect_survivors > baseline->effect_survivors) {
        out_res->status = RT_SURVIVOR_DIFF_NEW_EFFECT_SITE;
        out_res->reason = "gained surviving effect site";
        return RT_SURVIVOR_DIFF_NEW_EFFECT_SITE;
    }
    if (candidate->unproven_branches > baseline->unproven_branches) {
        out_res->status = RT_SURVIVOR_DIFF_NEW_UNPROVEN_BRANCH;
        out_res->reason = "gained unproven data-dependent branch";
        return RT_SURVIVOR_DIFF_NEW_UNPROVEN_BRANCH;
    }
    if (candidate->mono_instantiations > baseline->mono_instantiations ||
        candidate->mono_code_bytes > baseline->mono_code_bytes) {
        out_res->status = RT_SURVIVOR_DIFF_MONO_REGRESSION;
        out_res->reason = "monomorphization instantiation or code-size growth";
        return RT_SURVIVOR_DIFF_MONO_REGRESSION;
    }
    if (candidate->hw_dsp_blocks > 0u || candidate->dsp_mode != RT_BENCH_DSP_ZERO) {
        out_res->status = RT_SURVIVOR_DIFF_NONZERO_DSP;
        out_res->reason = "hardware kernel violates 0-DSP invariant";
        return RT_SURVIVOR_DIFF_NONZERO_DSP;
    }

    out_res->status = RT_SURVIVOR_DIFF_OK;
    out_res->reason = "no survivor regression";
    return RT_SURVIVOR_DIFF_OK;
}

#ifdef __cplusplus
}
#endif

#endif /* VERIOBJC_HOST_SURVIVOR_REPORT_H */
