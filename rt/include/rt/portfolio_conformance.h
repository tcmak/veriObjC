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
    if (consumer == VTPE_CONSUMER_SETUN_RTL_SIM) {
        return "setun";
    }
    if (consumer == VTPE_CONSUMER_KIT_M45_INT8_ORACLE) {
        return "veriobjc-inference-appliance-kit";
    }
    if (consumer == VTPE_CONSUMER_TERNARY_TAPE_JL) {
        return "TernaryTape.jl";
    }
    return "unknown";
}

static inline const char *vtpe_portfolio_clause_for_error(vtpe_v1_error_t err) {
    switch (err) {
    case VTPE_OK:
        return "§0.ok";
    case VTPE_ERR_NULL_OR_TRUNCATED:
        return "§1.header_framing";
    case VTPE_ERR_BAD_MAGIC:
        return "§1.header_magic";
    case VTPE_ERR_UNSUPPORTED_VERSION:
        return "§1.version_major";
    case VTPE_ERR_NONZERO_RESERVED:
        return "§1.reserved_zero";
    case VTPE_ERR_CHECKSUM_MISMATCH:
        return "§1.fnv1a32_checksum";
    case VTPE_ERR_UNKNOWN_FLAGS:
        return "§1.flags_adr0002_reserved_bits";
    case VTPE_ERR_UNSUPPORTED_PROFILE:
        return "§1.flags_adr0002_profile_bit2";
    case VTPE_ERR_SECTION_COUNT_MISMATCH:
        return "§2.section_count";
    case VTPE_ERR_LENGTH_MISMATCH:
        return "§2.total_length";
    case VTPE_ERR_ARTIFACT_ID_MISMATCH:
        return "§1.artifact_id_fnv1a64";
    case VTPE_ERR_UNKNOWN_TAG:
        return "§2.unknown_section_tag";
    case VTPE_ERR_DUPLICATE_TAG:
        return "§2.duplicate_section_tag";
    case VTPE_ERR_PROFILE_SECTION_FORBIDDEN:
        return "§2.profile_section_forbidden";
    case VTPE_ERR_MISSING_REQUIRED_SECTION:
        return "§2.missing_mandatory_section";
    case VTPE_ERR_MALFORMED_ARCH:
        return "§6.arch_descriptor";
    case VTPE_ERR_DIM_PAYLOAD_MISMATCH:
        return "§3.dim_payload_mismatch";
    case VTPE_ERR_RESERVED_TERNARY_CODE:
        return "§3.reserved_ternary_code_0b11";
    case VTPE_ERR_NONZERO_PADDING:
        return "§3.nonzero_tail_padding";
    case VTPE_ERR_INVALID_SCALE:
        return "§5.invalid_scale_exponent";
    case VTPE_ERR_MALFORMED_MANIFEST:
        return "§7.manifest_schema";
    case VTPE_ERR_INVALID_LINK:
        return "§4.self_relative_link_bounds";
    case VTPE_ERR_MALFORMED_FP32_AUX:
        return "§6.fp32_aux_schema";
    default:
        return "§8.unknown_error";
    }
}

static inline int vtpe_portfolio_verify_fixture(
    enum vtpe_portfolio_consumer consumer,
    const uint8_t *tape_bytes,
    size_t tape_len,
    vtpe_v1_error_t expected_err,
    struct vtpe_conformance_report *out_report) {
    if (!out_report) {
        return 0;
    }
    if (consumer != VTPE_CONSUMER_SETUN_RTL_SIM &&
        consumer != VTPE_CONSUMER_KIT_M45_INT8_ORACLE &&
        consumer != VTPE_CONSUMER_TERNARY_TAPE_JL) {
        return 0;
    }

    __builtin_memset(out_report, 0, sizeof(*out_report));
    out_report->consumer = consumer;
    out_report->repo_name = vtpe_portfolio_repo_name(consumer);
    out_report->expected_error = expected_err;

    vtpe_v1_profile_t reader_profile =
        (consumer == VTPE_CONSUMER_KIT_M45_INT8_ORACLE) ? VTPE_PROFILE_CORE : VTPE_PROFILE_EXTENDED;

    struct vtpe_reader reader;
    vtpe_v1_error_t actual = vtpe_reader_open(&reader, tape_bytes, tape_len, reader_profile);
    out_report->actual_error = actual;
    out_report->violated_clause = vtpe_portfolio_clause_for_error(actual);

    if (actual != expected_err) {
        out_report->passed = 0;
        return 0;
    }

    if (actual == VTPE_OK) {
        uint16_t dense_layer = (reader.layer_count == 3u) ? 2u : 0u;
        enum vtpe_backend_kind backend =
            (consumer == VTPE_CONSUMER_SETUN_RTL_SIM)
                ? VTPE_BACKEND_SETUN_LITEX
                : VTPE_BACKEND_KIT_HLS_TILE;

        const int32_t act4[4] = {12, 20, 28, 36};
        int32_t acc2[2] = {0, 0};
        int32_t scaled2[2] = {0, 0};

        struct vtpe_dispatch_ctx dctx;
        vtpe_dispatch_init(&dctx, backend, &reader);
        vtpe_scheduler_run_layer_matmul(&reader, &dctx, dense_layer, act4, acc2, scaled2);

        out_report->oracle_acc0 = acc2[0];
        out_report->oracle_acc1 = acc2[1];
        out_report->oracle_scaled0 = scaled2[0];
        out_report->oracle_scaled1 = scaled2[1];
    }

    out_report->passed = 1;
    return 1;
}

#ifdef __cplusplus
}
#endif

#endif /* VERIOBJC_RT_PORTFOLIO_CONFORMANCE_H */
