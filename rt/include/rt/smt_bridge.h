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
    (void)spec;
    (void)out_res;
    return RT_SMT_PROOF_INVALID_ARG;
}

#ifdef __cplusplus
}
#endif

#endif /* VERIOBJC_RT_SMT_BRIDGE_H */
