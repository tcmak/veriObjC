#include <CppUTest/TestHarness.h>
#include <cstring>

#include "host/survivor_report.h"

TEST_GROUP(SurvivorReportGroup) {};

TEST(SurvivorReportGroup, SurvivorDiffBlocksNewAllocEffectsUnprovenBranchesAndMonoRegressions) {
    struct rt_survivor_snapshot base;
    std::memset(&base, 0, sizeof(base));
    base.function_or_kernel = "fast_ring_publish";
    base.effect_survivors = 1u; // @fence only
    base.alloc_survivors = 0u;
    base.proven_branches = 1u;
    base.hinted_branches = 1u;
    base.unproven_branches = 0u;
    base.mono_instantiations = 1u;
    base.mono_code_bytes = 64u;
    base.hw_luts = 2048u;
    base.hw_ffs = 512u;
    base.hw_brams = 2u;
    base.hw_dsp_blocks = 0u;
    base.kernel_tier = RT_BENCH_TIER_CORE;
    base.dsp_mode = RT_BENCH_DSP_ZERO;

    struct rt_survivor_diff_result res;
    LONGS_EQUAL(RT_SURVIVOR_DIFF_OK, rt_survivor_diff_audit(&base, &base, &res));
    LONGS_EQUAL(RT_SURVIVOR_DIFF_OK, res.status);
    STRCMP_EQUAL("fast_ring_publish", res.offending_function);

    // 1. Adding an @alloc call site to a certified loop MUST fail CI
    struct rt_survivor_snapshot cand_alloc = base;
    cand_alloc.alloc_survivors = 1u;
    cand_alloc.effect_survivors = 2u;
    LONGS_EQUAL(RT_SURVIVOR_DIFF_NEW_ALLOC_EFFECT, rt_survivor_diff_audit(&base, &cand_alloc, &res));
    STRCMP_EQUAL("added @alloc call site in certified function", res.reason);

    // 2. Gaining a new non-alloc effect site fails review
    struct rt_survivor_snapshot cand_eff = base;
    cand_eff.effect_survivors = 2u;
    LONGS_EQUAL(RT_SURVIVOR_DIFF_NEW_EFFECT_SITE, rt_survivor_diff_audit(&base, &cand_eff, &res));
    STRCMP_EQUAL("gained surviving effect site", res.reason);

    // 3. Gaining an unproven data-dependent branch fails review
    struct rt_survivor_snapshot cand_branch = base;
    cand_branch.unproven_branches = 1u;
    LONGS_EQUAL(
        RT_SURVIVOR_DIFF_NEW_UNPROVEN_BRANCH,
        rt_survivor_diff_audit(&base, &cand_branch, &res));
    STRCMP_EQUAL("gained unproven data-dependent branch", res.reason);

    // 4. Gaining monomorphization instantiations or code bytes fails review
    struct rt_survivor_snapshot cand_mono_inst = base;
    cand_mono_inst.mono_instantiations = 2u;
    LONGS_EQUAL(
        RT_SURVIVOR_DIFF_MONO_REGRESSION,
        rt_survivor_diff_audit(&base, &cand_mono_inst, &res));

    struct rt_survivor_snapshot cand_mono_bytes = base;
    cand_mono_bytes.mono_code_bytes = 128u;
    LONGS_EQUAL(
        RT_SURVIVOR_DIFF_MONO_REGRESSION,
        rt_survivor_diff_audit(&base, &cand_mono_bytes, &res));

    // 5. Non-zero DSP blocks or DSP-enabled mode under zero-DSP gate fails review
    struct rt_survivor_snapshot cand_dsp = base;
    cand_dsp.hw_dsp_blocks = 1u;
    LONGS_EQUAL(RT_SURVIVOR_DIFF_NONZERO_DSP, rt_survivor_diff_audit(&base, &cand_dsp, &res));

    struct rt_survivor_snapshot cand_dsp_mode = base;
    cand_dsp_mode.dsp_mode = RT_BENCH_DSP_ENABLED;
    LONGS_EQUAL(RT_SURVIVOR_DIFF_NONZERO_DSP, rt_survivor_diff_audit(&base, &cand_dsp_mode, &res));

    // Defensive null / invalid checks
    LONGS_EQUAL(RT_SURVIVOR_DIFF_INVALID_ARG, rt_survivor_diff_audit(nullptr, &base, &res));
    LONGS_EQUAL(RT_SURVIVOR_DIFF_INVALID_ARG, rt_survivor_diff_audit(&base, nullptr, &res));
    LONGS_EQUAL(RT_SURVIVOR_DIFF_INVALID_ARG, rt_survivor_diff_audit(&base, &base, nullptr));

    struct rt_survivor_snapshot bad_fn = base;
    bad_fn.function_or_kernel = nullptr;
    LONGS_EQUAL(RT_SURVIVOR_DIFF_INVALID_ARG, rt_survivor_diff_audit(&bad_fn, &base, &res));
    LONGS_EQUAL(RT_SURVIVOR_DIFF_INVALID_ARG, rt_survivor_diff_audit(&base, &bad_fn, &res));

    struct rt_survivor_snapshot grp_prof = base;
    grp_prof.kernel_tier = RT_BENCH_TIER_GROUPED;
    LONGS_EQUAL(RT_SURVIVOR_DIFF_OK, rt_survivor_diff_audit(&base, &grp_prof, &res));

    struct rt_survivor_snapshot ext_prof = base;
    ext_prof.kernel_tier = RT_BENCH_TIER_EXTENDED;
    LONGS_EQUAL(RT_SURVIVOR_DIFF_OK, rt_survivor_diff_audit(&base, &ext_prof, &res));

    struct rt_survivor_snapshot bad_prof = base;
    bad_prof.kernel_tier = static_cast<enum rt_bench_kernel_tier>(99);
    LONGS_EQUAL(RT_SURVIVOR_DIFF_INVALID_ARG, rt_survivor_diff_audit(&base, &bad_prof, &res));
}
