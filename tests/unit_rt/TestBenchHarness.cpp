#include <CppUTest/TestHarness.h>
#include <cstring>
#include <string>

#include "host/bench_harness.h"
#include "vstd/vector.h"

TEST_GROUP(BenchHarnessGroup) {};

TEST(BenchHarnessGroup, CycleCounterAndPercentileStatsOnVVectorContainer) {
    uint64_t c0 = rt_bench_read_cycles();
    vvector<int32_t, 4> vec;
    vvector_init(&vec);
    vvector_push(&vec, 10);
    vvector_push(&vec, 20);
    uint64_t c1 = rt_bench_read_cycles();
    CHECK(c0 > 0u);
    CHECK(c1 >= c0);

    uint64_t samples[10] = {40, 10, 30, 20, 50, 90, 80, 70, 60, 100};
    struct rt_bench_stats st;
    LONGS_EQUAL(1, rt_bench_compute_stats(samples, 10, 12, &st));
    LONGS_EQUAL(10u, st.min_cycles);
    LONGS_EQUAL(50u, st.p50_cycles);
    LONGS_EQUAL(100u, st.p99_cycles);
    LONGS_EQUAL(100u, st.p999_cycles);
    LONGS_EQUAL(100u, st.max_cycles);
    LONGS_EQUAL(12u, st.teardown_cycles);

    // Defensive null / empty checks
    LONGS_EQUAL(0, rt_bench_compute_stats(nullptr, 10, 0, &st));
    LONGS_EQUAL(0, rt_bench_compute_stats(samples, 0, 0, &st));
    LONGS_EQUAL(0, rt_bench_compute_stats(samples, 10, 0, nullptr));
}

TEST(BenchHarnessGroup, Amendment4KitM6MethodologyValidationAndFormatting) {
    struct rt_bench_m6_record rec;
    std::memset(&rec, 0, sizeof(rec));
    rec.bench_id = "vstd.vvector_i32x4.push_and_dot";
    rec.shape_m = 2;
    rec.shape_n = 1;
    rec.shape_k = 4;
    rec.cited_baseline = "clang23-scalar-ref";
    rec.dollar_norm_cents = 4900u;
    rec.kernel_tier = RT_BENCH_TIER_CORE;
    rec.dsp_mode = RT_BENCH_DSP_ZERO;
    rec.stats.min_cycles = 10;
    rec.stats.p50_cycles = 14;
    rec.stats.p99_cycles = 18;
    rec.stats.p999_cycles = 20;
    rec.stats.max_cycles = 20;
    rec.stats.teardown_cycles = 4;

    LONGS_EQUAL(1, rt_bench_validate_m6_record(&rec));

    char line[256];
    LONGS_EQUAL(1, rt_bench_format_m6_line(&rec, line, sizeof(line)));
    std::string s(line);
    CHECK(s.find("bench_id=vstd.vvector_i32x4.push_and_dot") != std::string::npos);
    CHECK(s.find("shape=2x1x4") != std::string::npos);
    CHECK(s.find("baseline=clang23-scalar-ref") != std::string::npos);
    CHECK(s.find("dollar_cents=4900") != std::string::npos);
    CHECK(s.find("kernel_tier=core") != std::string::npos);
    CHECK(s.find("dsp_mode=zero-dsp") != std::string::npos);
    CHECK(s.find("p50=14") != std::string::npos);
    CHECK(s.find("p99=18") != std::string::npos);
    CHECK(s.find("teardown=4") != std::string::npos);

    // Grouped and Extended tier + DSP enabled formatting branches
    struct rt_bench_m6_record grp_rec = rec;
    grp_rec.kernel_tier = RT_BENCH_TIER_GROUPED;
    LONGS_EQUAL(1, rt_bench_format_m6_line(&grp_rec, line, sizeof(line)));
    CHECK(std::string(line).find("kernel_tier=grouped") != std::string::npos);

    struct rt_bench_m6_record ext_rec = rec;
    ext_rec.kernel_tier = RT_BENCH_TIER_EXTENDED;
    ext_rec.dsp_mode = RT_BENCH_DSP_ENABLED;
    LONGS_EQUAL(1, rt_bench_format_m6_line(&ext_rec, line, sizeof(line)));
    std::string s_ext(line);
    CHECK(s_ext.find("kernel_tier=extended") != std::string::npos);
    CHECK(s_ext.find("dsp_mode=dsp-enabled") != std::string::npos);

    // Rejection of incomplete M6 records (missing baseline, shape, $-norm, tier, dsp_mode)
    LONGS_EQUAL(0, rt_bench_validate_m6_record(nullptr));
    struct rt_bench_m6_record bad = rec;
    bad.bench_id = nullptr;
    LONGS_EQUAL(0, rt_bench_validate_m6_record(&bad));
    bad = rec;
    bad.bench_id = "";
    LONGS_EQUAL(0, rt_bench_validate_m6_record(&bad));
    bad = rec;
    bad.shape_m = 0;
    LONGS_EQUAL(0, rt_bench_validate_m6_record(&bad));
    bad = rec;
    bad.shape_n = 0;
    LONGS_EQUAL(0, rt_bench_validate_m6_record(&bad));
    bad = rec;
    bad.shape_k = 0;
    LONGS_EQUAL(0, rt_bench_validate_m6_record(&bad));
    bad = rec;
    bad.cited_baseline = nullptr;
    LONGS_EQUAL(0, rt_bench_validate_m6_record(&bad));
    bad = rec;
    bad.cited_baseline = "";
    LONGS_EQUAL(0, rt_bench_validate_m6_record(&bad));
    bad = rec;
    bad.dollar_norm_cents = 0;
    LONGS_EQUAL(0, rt_bench_validate_m6_record(&bad));
    bad = rec;
    bad.kernel_tier = static_cast<enum rt_bench_kernel_tier>(99);
    LONGS_EQUAL(0, rt_bench_validate_m6_record(&bad));
    bad = rec;
    bad.dsp_mode = static_cast<enum rt_bench_dsp_mode>(99);
    LONGS_EQUAL(0, rt_bench_validate_m6_record(&bad));
    bad = rec;
    bad.stats.p50_cycles = 0;
    LONGS_EQUAL(0, rt_bench_validate_m6_record(&bad));
    bad = rec;
    bad.stats.p99_cycles = 10; // < p50_cycles (14)
    LONGS_EQUAL(0, rt_bench_validate_m6_record(&bad));

    LONGS_EQUAL(0, rt_bench_format_m6_line(&bad, line, sizeof(line)));
    LONGS_EQUAL(0, rt_bench_format_m6_line(&rec, nullptr, sizeof(line)));
    LONGS_EQUAL(0, rt_bench_format_m6_line(&rec, line, 10)); // buffer too small
}
