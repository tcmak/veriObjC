#include <CppUTest/TestHarness.h>
#include <cstring>

#include "host/smt_bridge.h"

TEST_GROUP(SmtContractBridgeGroup) {};

TEST(SmtContractBridgeGroup, EvaluatesAffineContractEndpointsStubWithoutClaimingSmtVerification) {
    // Valid affine endpoint check: @pre(0 <= msg_len <= 1400), out = 2 * msg_len + 64, @post(64 <= out <= 2864)
    struct rt_smt_contract_spec valid_spec;
    valid_spec.function_name = "export_ingest_wire_order";
    valid_spec.var_name = "hdr_msg_len";
    valid_spec.pre_lower_bound = 0;
    valid_spec.pre_upper_bound = 1400;
    valid_spec.transform_scale = 2;
    valid_spec.transform_offset = 64;
    valid_spec.post_lower_bound = 64;
    valid_spec.post_upper_bound = 2864;
    valid_spec.pre_smt_expr = "(and (>= hdr_msg_len 0) (<= hdr_msg_len 1400))";
    valid_spec.post_smt_expr = "(and (>= ret 64) (<= ret 2864))";

    struct rt_contract_endpoint_result res;
    LONGS_EQUAL(RT_CONTRACT_ENDPOINT_CHECK_PASSED, rt_contract_eval_endpoints_stub(&valid_spec, &res));
    LONGS_EQUAL(RT_CONTRACT_ENDPOINT_CHECK_PASSED, res.status);
    CHECK(std::strstr(res.smtlib2_script, "(set-logic QF_LIA)") != nullptr);
    CHECK(std::strstr(res.smtlib2_script, "(declare-const hdr_msg_len Int)") != nullptr);
    CHECK(std::strstr(res.smtlib2_script, "(assert (and (>= hdr_msg_len 0) (<= hdr_msg_len 1400)))") != nullptr);
    CHECK(std::strstr(res.smtlib2_script, "(assert (not (and (>= ret 64) (<= ret 2864))))") != nullptr);
    CHECK(std::strstr(res.smtlib2_script, "UNIMPLEMENTED_STUB") != nullptr);
    CHECK(std::strstr(res.smtlib2_script, "(check-sat)") == nullptr);

    // Violated upper bound at upper endpoint
    struct rt_smt_contract_spec fail_upper = valid_spec;
    fail_upper.post_upper_bound = 2800; // 2*1400+64 = 2864 > 2800
    LONGS_EQUAL(RT_CONTRACT_ENDPOINT_COUNTEREXAMPLE, rt_contract_eval_endpoints_stub(&fail_upper, &res));
    LONGS_EQUAL(1400, res.counterexample_input);
    LONGS_EQUAL(2864, res.counterexample_output);

    // Violated lower bound at lower endpoint
    struct rt_smt_contract_spec fail_lower = valid_spec;
    fail_lower.post_lower_bound = 100; // 2*0+64 = 64 < 100
    LONGS_EQUAL(RT_CONTRACT_ENDPOINT_COUNTEREXAMPLE, rt_contract_eval_endpoints_stub(&fail_lower, &res));
    LONGS_EQUAL(0, res.counterexample_input);
    LONGS_EQUAL(64, res.counterexample_output);

    // Negative scale transform flips lower/upper image endpoints
    struct rt_smt_contract_spec neg_scale = valid_spec;
    neg_scale.transform_scale = -1;
    neg_scale.transform_offset = 2000;
    neg_scale.post_lower_bound = 600;
    neg_scale.post_upper_bound = 2000;
    LONGS_EQUAL(RT_CONTRACT_ENDPOINT_CHECK_PASSED, rt_contract_eval_endpoints_stub(&neg_scale, &res));

    // Invalid / null argument checks
    LONGS_EQUAL(RT_CONTRACT_ENDPOINT_INVALID_ARG, rt_contract_eval_endpoints_stub(nullptr, &res));
    LONGS_EQUAL(RT_CONTRACT_ENDPOINT_INVALID_ARG, rt_contract_eval_endpoints_stub(&valid_spec, nullptr));

    struct rt_smt_contract_spec bad_bounds = valid_spec;
    bad_bounds.pre_lower_bound = 1500;
    bad_bounds.pre_upper_bound = 1400;
    LONGS_EQUAL(RT_CONTRACT_ENDPOINT_INVALID_ARG, rt_contract_eval_endpoints_stub(&bad_bounds, &res));

    struct rt_smt_contract_spec null_fn = valid_spec;
    null_fn.function_name = nullptr;
    LONGS_EQUAL(RT_CONTRACT_ENDPOINT_INVALID_ARG, rt_contract_eval_endpoints_stub(&null_fn, &res));

    struct rt_smt_contract_spec null_var = valid_spec;
    null_var.var_name = nullptr;
    LONGS_EQUAL(RT_CONTRACT_ENDPOINT_INVALID_ARG, rt_contract_eval_endpoints_stub(&null_var, &res));

    struct rt_smt_contract_spec null_pre = valid_spec;
    null_pre.pre_smt_expr = nullptr;
    LONGS_EQUAL(RT_CONTRACT_ENDPOINT_INVALID_ARG, rt_contract_eval_endpoints_stub(&null_pre, &res));

    struct rt_smt_contract_spec null_post = valid_spec;
    null_post.post_smt_expr = nullptr;
    LONGS_EQUAL(RT_CONTRACT_ENDPOINT_INVALID_ARG, rt_contract_eval_endpoints_stub(&null_post, &res));
}

TEST(SmtContractBridgeGroup, DischargesValidBitVectorAndLiaContractsViaRealZ3) {
    // 1. Exact 32-bit signed bit-vector proof: 0 <= x <= 1000 => 16 <= 2*x + 16 <= 2016
    const char *bv_unsat_query =
        "(set-option :produce-models true)\n"
        "(set-logic QF_BV)\n"
        "(declare-const x (_ BitVec 32))\n"
        "(declare-const result (_ BitVec 32))\n"
        "(assert (and (bvsge x #x00000000) (bvsle x #x000003e8)))\n"
        "(assert (= result (bvadd (bvmul x #x00000002) #x00000010)))\n"
        "(assert (not (and (bvsge result #x00000010) (bvsle result #x000007e0))))\n"
        "(check-sat)\n"
        "(get-value (x result))\n";

    struct rt_z3_discharge_result res;
    LONGS_EQUAL(Z3_UNSAT_PROVEN, rt_z3_discharge_smtlib2(nullptr, 2000u, bv_unsat_query, &res));
    LONGS_EQUAL(Z3_UNSAT_PROVEN, res.status);
    CHECK(std::strstr(res.solver_version, "Z3 version ") != nullptr);
    LONGS_EQUAL(0u, res.witness_count);

    // 2. Linear integer arithmetic (QF_LIA) proof
    const char *lia_unsat_query =
        "(set-option :produce-models true)\n"
        "(set-logic QF_LIA)\n"
        "(declare-const qty Int)\n"
        "(declare-const result Int)\n"
        "(assert (and (>= qty 1) (<= qty 1000)))\n"
        "(assert (= result (ite (and (>= qty 1) (<= qty 1000)) qty 0)))\n"
        "(assert (not (>= result 1)))\n"
        "(check-sat)\n";

    LONGS_EQUAL(Z3_UNSAT_PROVEN, rt_z3_discharge_smtlib2("", 0u, lia_unsat_query, &res));
    LONGS_EQUAL(Z3_UNSAT_PROVEN, res.status);
}

TEST(SmtContractBridgeGroup, ExtractsConcreteCounterexampleWitnessesAcrossZ3Versions) {
    // 1. 32-bit signed overflow counterexample in real Z3:
    //    @pre(price >= 0 && margin >= 1 && margin <= 100), result = price + margin, @post(result > price)
    const char *bv_overflow_sat_query =
        "(set-option :produce-models true)\n"
        "(set-logic QF_BV)\n"
        "(declare-const price (_ BitVec 32))\n"
        "(declare-const margin (_ BitVec 32))\n"
        "(declare-const result (_ BitVec 32))\n"
        "(assert (bvsge price #x00000000))\n"
        "(assert (and (bvsge margin #x00000001) (bvsle margin #x00000064)))\n"
        "(assert (= result (bvadd price margin)))\n"
        "(assert (not (bvsgt result price)))\n"
        "(check-sat)\n"
        "(get-value (price margin result))\n";

    struct rt_z3_discharge_result res;
    LONGS_EQUAL(Z3_SAT_COUNTEREXAMPLE, rt_z3_discharge_smtlib2(nullptr, 2000u, bv_overflow_sat_query, &res));
    LONGS_EQUAL(Z3_SAT_COUNTEREXAMPLE, res.status);
    LONGS_EQUAL(3u, res.witness_count);
    STRCMP_EQUAL("price", res.witnesses[0].name);
    STRCMP_EQUAL("margin", res.witnesses[1].name);
    STRCMP_EQUAL("result", res.witnesses[2].name);
    CHECK_TRUE(res.witnesses[0].has_numeric_val);
    CHECK_TRUE(res.witnesses[1].has_numeric_val);
    CHECK_TRUE(res.witnesses[2].has_numeric_val);
    // Witness must satisfy 32-bit signed wrap: (int32_t)result <= (int32_t)price while price >= 0 and 1 <= margin <= 100
    int32_t w_price = (int32_t)res.witnesses[0].unsigned_val;
    int32_t w_margin = (int32_t)res.witnesses[1].unsigned_val;
    int32_t w_result = (int32_t)res.witnesses[2].unsigned_val;
    CHECK(w_price >= 0);
    CHECK(w_margin >= 1 && w_margin <= 100);
    LONGS_EQUAL((int32_t)((uint32_t)w_price + (uint32_t)w_margin), w_result);
    CHECK(w_result <= w_price);

    // 2. QF_LIA negative integer witness (- 5)
    const char *lia_sat_query =
        "(set-option :produce-models true)\n"
        "(set-logic QF_LIA)\n"
        "(declare-const delta Int)\n"
        "(assert (= delta (- 5)))\n"
        "(check-sat)\n"
        "(get-value (delta))\n";
    LONGS_EQUAL(Z3_SAT_COUNTEREXAMPLE, rt_z3_discharge_smtlib2(nullptr, 2000u, lia_sat_query, &res));
    LONGS_EQUAL(1u, res.witness_count);
    STRCMP_EQUAL("delta", res.witnesses[0].name);
    LONGS_EQUAL(-5, res.witnesses[0].signed_val);

    // 3. Cross-version model output compatibility: Z3 4.8.12 `(model (define-fun ...))` AND Z3 4.13 `((define-fun ...))`
    const char *z3_4_8_model =
        "sat\n(model \n  (define-fun idx () (_ BitVec 32)\n    #x00000040)\n  (define-fun flag () Bool\n    true)\n)\n";
    const char *z3_4_13_model =
        "sat\n(\n  (define-fun idx () (_ BitVec 32)\n    #x00000040)\n  (define-fun bits () (_ BitVec 4)\n    #b1010)\n)\n";

    struct rt_z3_discharge_result parsed_4_8;
    rt_z3_parse_solver_output(z3_4_8_model, &parsed_4_8);
    LONGS_EQUAL(Z3_SAT_COUNTEREXAMPLE, parsed_4_8.status);
    LONGS_EQUAL(2u, parsed_4_8.witness_count);
    STRCMP_EQUAL("idx", parsed_4_8.witnesses[0].name);
    LONGS_EQUAL(64u, parsed_4_8.witnesses[0].unsigned_val);
    STRCMP_EQUAL("flag", parsed_4_8.witnesses[1].name);
    LONGS_EQUAL(1u, parsed_4_8.witnesses[1].unsigned_val);

    struct rt_z3_discharge_result parsed_4_13;
    rt_z3_parse_solver_output(z3_4_13_model, &parsed_4_13);
    LONGS_EQUAL(Z3_SAT_COUNTEREXAMPLE, parsed_4_13.status);
    LONGS_EQUAL(2u, parsed_4_13.witness_count);
    STRCMP_EQUAL("idx", parsed_4_13.witnesses[0].name);
    LONGS_EQUAL(64u, parsed_4_13.witnesses[0].unsigned_val);
    STRCMP_EQUAL("bits", parsed_4_13.witnesses[1].name);
    LONGS_EQUAL(10u, parsed_4_13.witnesses[1].unsigned_val);
}

TEST(SmtContractBridgeGroup, FailsClosedWithUnknownTimeoutWhenBudgetExceeded) {
    // Hard 64-bit non-linear bit-vector factorization with a 15ms timeout must fail closed as Z3_UNKNOWN_TIMEOUT
    const char *hard_bv_query =
        "(set-logic QF_BV)\n"
        "(declare-const a (_ BitVec 64))\n"
        "(declare-const b (_ BitVec 64))\n"
        "(assert (= (bvmul a b) #x0123456789abcdef))\n"
        "(assert (bvugt a #x0000000000000001))\n"
        "(assert (bvugt b #x0000000000000001))\n"
        "(assert (= (bvurem a #x0000000000000002) #x0000000000000001))\n"
        "(check-sat)\n";

    struct rt_z3_discharge_result res;
    LONGS_EQUAL(Z3_UNKNOWN_TIMEOUT, rt_z3_discharge_smtlib2(nullptr, 15u, hard_bv_query, &res));
    LONGS_EQUAL(Z3_UNKNOWN_TIMEOUT, res.status);
}

TEST(SmtContractBridgeGroup, FailsClosedWhenZ3BinaryMissingOrInvalid) {
    const char *simple_query = "(set-logic QF_LIA)\n(declare-const x Int)\n(assert (> x 0))\n(check-sat)\n";
    struct rt_z3_discharge_result res;

    // 1. Nonexistent path -> Z3_SOLVER_UNAVAILABLE naming the bad path and >= 4.8.12 requirement
    LONGS_EQUAL(Z3_SOLVER_UNAVAILABLE, rt_z3_discharge_smtlib2("/nonexistent/veriobjc/z3", 500u, simple_query, &res));
    LONGS_EQUAL(Z3_SOLVER_UNAVAILABLE, res.status);
    CHECK(std::strstr(res.diagnostic_detail, "/nonexistent/veriobjc/z3") != nullptr);
    CHECK(std::strstr(res.diagnostic_detail, "4.8.12") != nullptr);

    // 2. Non-Z3 executable (/bin/true) -> Z3_SOLVER_UNAVAILABLE
    LONGS_EQUAL(Z3_SOLVER_UNAVAILABLE, rt_z3_discharge_smtlib2("/bin/true", 500u, simple_query, &res));
    LONGS_EQUAL(Z3_SOLVER_UNAVAILABLE, res.status);

    // 3. Invalid / null query arguments -> Z3_INVALID_QUERY
    LONGS_EQUAL(Z3_INVALID_QUERY, rt_z3_discharge_smtlib2(nullptr, 500u, nullptr, &res));
    LONGS_EQUAL(Z3_INVALID_QUERY, rt_z3_discharge_smtlib2(nullptr, 500u, "", &res));
    LONGS_EQUAL(Z3_INVALID_QUERY, rt_z3_discharge_smtlib2(nullptr, 500u, simple_query, nullptr));

    // 4. Version comparator helper checks (4.8.11 < 4.8.12 <= 4.8.12 <= 4.13.3)
    CHECK_FALSE(rt_z3_version_at_least("Z3 version 4.8.11 - 64 bit", 4u, 8u, 12u));
    CHECK_TRUE(rt_z3_version_at_least("Z3 version 4.8.12 - 64 bit", 4u, 8u, 12u));
    CHECK_TRUE(rt_z3_version_at_least("Z3 version 4.13.0 - 64 bit", 4u, 8u, 12u));
    CHECK_TRUE(rt_z3_version_at_least("Z3 version 5.0.0 - 64 bit", 4u, 8u, 12u));
    CHECK_FALSE(rt_z3_version_at_least("invalid output", 4u, 8u, 12u));
    CHECK_FALSE(rt_z3_version_at_least(nullptr, 4u, 8u, 12u));
}
