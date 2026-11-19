#include <CppUTest/TestHarness.h>
#include <cstring>

#include "rt/latency_gate.h"

TEST_GROUP(LatencyBudgetGateGroup) {};

TEST(LatencyBudgetGateGroup, LatencyGatePassesWithinNoiseBandAndFailsClosedOnRegressionNamingCommitAndBench) {
    struct rt_latency_budget budget;
    budget.bench_id = "vvector_push_pop_64";
    budget.max_p50_cycles = 20u;
    budget.max_p99_cycles = 40u;
    budget.max_p999_cycles = 80u;
    budget.max_teardown_cycles = 5u;
    budget.noise_band_cycles = 3u;

    struct rt_bench_m6_record rec;
    std::memset(&rec, 0, sizeof(rec));
    rec.bench_id = "vvector_push_pop_64";
    rec.shape_m = 4u;
    rec.shape_n = 4u;
    rec.shape_k = 16u;
    rec.cited_baseline = "clang23_std_vector_O3";
    rec.dollar_norm_cents = 29900u;
    rec.vtpe_profile = VTPE_PROFILE_CORE;
    rec.dsp_mode = RT_BENCH_DSP_ZERO;
    rec.stats.min_cycles = 16u;
    rec.stats.p50_cycles = 22u;  // 20 + 2 <= 20 + 3 (within noise band)
    rec.stats.p99_cycles = 43u;  // 40 + 3 <= 40 + 3 (at noise band edge)
    rec.stats.p999_cycles = 80u;
    rec.stats.max_cycles = 82u;
    rec.stats.teardown_cycles = 4u;

    struct rt_latency_gate_report rep;
    LONGS_EQUAL(RT_LATENCY_GATE_OK, rt_latency_gate_check(&budget, &rec, "9585644", &rep));
    LONGS_EQUAL(RT_LATENCY_GATE_OK, rep.status);
    STRCMP_EQUAL("vvector_push_pop_64", rep.offending_bench_id);
    STRCMP_EQUAL("9585644", rep.offending_commit_id);
    CHECK(std::strstr(rep.diagnostic, "PASS") != nullptr);

    // 1. p50 regression beyond noise band fails closed naming benchmark and commit
    struct rt_bench_m6_record p50_reg = rec;
    p50_reg.stats.p50_cycles = 24u; // > 20 + 3
    LONGS_EQUAL(RT_LATENCY_GATE_P50_EXCEEDED, rt_latency_gate_check(&budget, &p50_reg, "deadbeef", &rep));
    LONGS_EQUAL(24u, rep.measured_cycles);
    LONGS_EQUAL(23u, rep.ceiling_cycles);
    CHECK(std::strstr(rep.diagnostic, "bench=vvector_push_pop_64") != nullptr);
    CHECK(std::strstr(rep.diagnostic, "commit=deadbeef") != nullptr);
    CHECK(std::strstr(rep.diagnostic, "metric=p50") != nullptr);

    // 2. p99 regression beyond noise band fails closed
    struct rt_bench_m6_record p99_reg = rec;
    p99_reg.stats.p99_cycles = 44u; // > 40 + 3
    LONGS_EQUAL(RT_LATENCY_GATE_P99_EXCEEDED, rt_latency_gate_check(&budget, &p99_reg, "cafebabe", &rep));
    LONGS_EQUAL(44u, rep.measured_cycles);
    LONGS_EQUAL(43u, rep.ceiling_cycles);
    CHECK(std::strstr(rep.diagnostic, "metric=p99") != nullptr);

    // 3. p99.9 regression beyond noise band fails closed
    struct rt_bench_m6_record p999_reg = rec;
    p999_reg.stats.p999_cycles = 85u; // > 80 + 3
    LONGS_EQUAL(RT_LATENCY_GATE_P999_EXCEEDED, rt_latency_gate_check(&budget, &p999_reg, "feedface", &rep));
    LONGS_EQUAL(85u, rep.measured_cycles);
    LONGS_EQUAL(83u, rep.ceiling_cycles);
    CHECK(std::strstr(rep.diagnostic, "metric=p999") != nullptr);

    // 4. teardown regression beyond noise band fails closed
    struct rt_bench_m6_record td_reg = rec;
    td_reg.stats.teardown_cycles = 9u; // > 5 + 3
    LONGS_EQUAL(RT_LATENCY_GATE_TEARDOWN_EXCEEDED, rt_latency_gate_check(&budget, &td_reg, "badc0de", &rep));
    LONGS_EQUAL(9u, rep.measured_cycles);
    LONGS_EQUAL(8u, rep.ceiling_cycles);
    CHECK(std::strstr(rep.diagnostic, "metric=teardown") != nullptr);

    // 5. Mismatched bench_id or invalid M6 metadata fails closed
    struct rt_bench_m6_record wrong_bench = rec;
    wrong_bench.bench_id = "other_bench";
    LONGS_EQUAL(RT_LATENCY_GATE_BENCH_MISMATCH, rt_latency_gate_check(&budget, &wrong_bench, "9585644", &rep));

    struct rt_bench_m6_record bad_m6 = rec;
    bad_m6.shape_m = 0u;
    LONGS_EQUAL(RT_LATENCY_GATE_M6_INVALID, rt_latency_gate_check(&budget, &bad_m6, "9585644", &rep));

    // 6. Null / empty arguments fail closed
    LONGS_EQUAL(RT_LATENCY_GATE_INVALID_ARG, rt_latency_gate_check(nullptr, &rec, "9585644", &rep));
    LONGS_EQUAL(RT_LATENCY_GATE_INVALID_ARG, rt_latency_gate_check(&budget, nullptr, "9585644", &rep));
    LONGS_EQUAL(RT_LATENCY_GATE_INVALID_ARG, rt_latency_gate_check(&budget, &rec, nullptr, &rep));
    LONGS_EQUAL(RT_LATENCY_GATE_INVALID_ARG, rt_latency_gate_check(&budget, &rec, "", &rep));
    LONGS_EQUAL(RT_LATENCY_GATE_INVALID_ARG, rt_latency_gate_check(&budget, &rec, "9585644", nullptr));

    struct rt_latency_budget null_id_budget = budget;
    null_id_budget.bench_id = nullptr;
    LONGS_EQUAL(RT_LATENCY_GATE_INVALID_ARG, rt_latency_gate_check(&null_id_budget, &rec, "9585644", &rep));
}
