#ifndef VERIOBJC_RT_SMT_BRIDGE_H
#define VERIOBJC_RT_SMT_BRIDGE_H

#include "rt/bench_harness.h"

#ifdef __cplusplus
extern "C" {
#endif

enum rt_smt_proof_status {
    RT_SMT_PROOF_UNSAT_VERIFIED = 0,
    RT_SMT_PROOF_SAT_COUNTEREXAMPLE = 1,
    RT_SMT_PROOF_INVALID_ARG = 2
};

struct rt_smt_contract_spec {
    const char *function_name;
    const char *var_name;
    int64_t pre_lower_bound;
    int64_t pre_upper_bound;
    int64_t transform_scale;
    int64_t transform_offset;
    int64_t post_lower_bound;
    int64_t post_upper_bound;
    const char *pre_smt_expr;
    const char *post_smt_expr;
};

struct rt_smt_proof_result {
    enum rt_smt_proof_status status;
    int64_t counterexample_input;
    int64_t counterexample_output;
    char smtlib2_script[512];
};

static inline enum rt_smt_proof_status rt_smt_verify_contract_vc(
    const struct rt_smt_contract_spec *spec,
    struct rt_smt_proof_result *out_res) {
    if (!spec || !out_res || !spec->function_name || !spec->var_name ||
        !spec->pre_smt_expr || !spec->post_smt_expr ||
        spec->pre_lower_bound > spec->pre_upper_bound) {
        return RT_SMT_PROOF_INVALID_ARG;
    }

    size_t pos = 0u;
    pos = rt_bench_append_str(out_res->smtlib2_script, pos, "(set-logic QF_LIA)\n(declare-const ");
    pos = rt_bench_append_str(out_res->smtlib2_script, pos, spec->var_name);
    pos = rt_bench_append_str(out_res->smtlib2_script, pos, " Int)\n(declare-const ret Int)\n(assert ");
    pos = rt_bench_append_str(out_res->smtlib2_script, pos, spec->pre_smt_expr);
    pos = rt_bench_append_str(out_res->smtlib2_script, pos, ")\n(assert (not ");
    pos = rt_bench_append_str(out_res->smtlib2_script, pos, spec->post_smt_expr);
    pos = rt_bench_append_str(out_res->smtlib2_script, pos, "))\n(check-sat)");
    out_res->smtlib2_script[pos] = '\0';

    int64_t out_at_low = spec->transform_scale * spec->pre_lower_bound + spec->transform_offset;
    int64_t out_at_high = spec->transform_scale * spec->pre_upper_bound + spec->transform_offset;

    int64_t min_in = (out_at_low <= out_at_high) ? spec->pre_lower_bound : spec->pre_upper_bound;
    int64_t min_out = (out_at_low <= out_at_high) ? out_at_low : out_at_high;
    int64_t max_in = (out_at_low <= out_at_high) ? spec->pre_upper_bound : spec->pre_lower_bound;
    int64_t max_out = (out_at_low <= out_at_high) ? out_at_high : out_at_low;

    if (min_out < spec->post_lower_bound) {
        out_res->status = RT_SMT_PROOF_SAT_COUNTEREXAMPLE;
        out_res->counterexample_input = min_in;
        out_res->counterexample_output = min_out;
        return RT_SMT_PROOF_SAT_COUNTEREXAMPLE;
    }
    if (max_out > spec->post_upper_bound) {
        out_res->status = RT_SMT_PROOF_SAT_COUNTEREXAMPLE;
        out_res->counterexample_input = max_in;
        out_res->counterexample_output = max_out;
        return RT_SMT_PROOF_SAT_COUNTEREXAMPLE;
    }

    out_res->status = RT_SMT_PROOF_UNSAT_VERIFIED;
    out_res->counterexample_input = 0;
    out_res->counterexample_output = 0;
    return RT_SMT_PROOF_UNSAT_VERIFIED;
}

#ifdef __cplusplus
}
#endif

#endif /* VERIOBJC_RT_SMT_BRIDGE_H */
