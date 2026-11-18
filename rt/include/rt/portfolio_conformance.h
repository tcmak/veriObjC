#ifndef VERIOBJC_RT_PORTFOLIO_CONFORMANCE_H
#define VERIOBJC_RT_PORTFOLIO_CONFORMANCE_H

#include "rt/scheduler.h"

#ifdef __cplusplus
extern "C" {
#endif

enum vtpe_portfolio_consumer {
    VTPE_CONSUMER_SETUN_RTL_SIM = 1,
    VTPE_CONSUMER_KIT_M45_INT8_ORACLE = 2,
    VTPE_CONSUMER_TERNARY_TAPE_JL = 3
};

struct vtpe_conformance_report {
    enum vtpe_portfolio_consumer consumer;
    const char *repo_name;
    const char *violated_clause;
    vtpe_v1_error_t actual_error;
    vtpe_v1_error_t expected_error;
    int passed;
    int32_t oracle_acc0;
    int32_t oracle_acc1;
    int32_t oracle_scaled0;
    int32_t oracle_scaled1;
};

static inline const char *vtpe_portfolio_repo_name(enum vtpe_portfolio_consumer consumer) {
    (void)consumer;
    return "unknown";
}

static inline const char *vtpe_portfolio_clause_for_error(vtpe_v1_error_t err) {
    (void)err;
    return "§0.none";
}

static inline int vtpe_portfolio_verify_fixture(
    enum vtpe_portfolio_consumer consumer,
    const uint8_t *tape_bytes,
    size_t tape_len,
    vtpe_v1_error_t expected_err,
    struct vtpe_conformance_report *out_report) {
    (void)consumer;
    (void)tape_bytes;
    (void)tape_len;
    (void)expected_err;
    (void)out_report;
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* VERIOBJC_RT_PORTFOLIO_CONFORMANCE_H */
