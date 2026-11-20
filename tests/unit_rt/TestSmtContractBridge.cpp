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
