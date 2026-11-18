#include <CppUTest/TestHarness.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "rt/portfolio_conformance.h"

namespace {

std::vector<uint8_t> read_tape(const char *rel_path) {
    std::string full = std::string(VERIOBJC_SOURCE_DIR) + "/" + rel_path;
    FILE *fp = std::fopen(full.c_str(), "rb");
    CHECK_TEXT(fp != nullptr, full.c_str());
    std::fseek(fp, 0, SEEK_END);
    long sz = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    std::vector<uint8_t> out(static_cast<size_t>(sz));
    if (sz > 0) {
        size_t n = std::fread(out.data(), 1, static_cast<size_t>(sz), fp);
        LONGS_EQUAL(static_cast<size_t>(sz), n);
    }
    std::fclose(fp);
    return out;
}

struct GoldenCase {
    const char *path;
    vtpe_v1_error_t expected_core;
    vtpe_v1_error_t expected_extended;
};

const GoldenCase kGoldenCorpus[12] = {
    {"fixtures/vtpe_v1/accept/core_minimal_dot4.bin", VTPE_OK, VTPE_OK},
    {"fixtures/vtpe_v1/accept/core_with_manifest_and_links.bin", VTPE_OK, VTPE_OK},
    {"fixtures/vtpe_v1/accept/extended_with_fp32_aux.bin", VTPE_ERR_UNSUPPORTED_PROFILE, VTPE_OK},
    {"fixtures/vtpe_v1/reject/bad_magic.bin", VTPE_ERR_BAD_MAGIC, VTPE_ERR_BAD_MAGIC},
    {"fixtures/vtpe_v1/reject/unknown_flags_bit3.bin", VTPE_ERR_UNKNOWN_FLAGS, VTPE_ERR_UNKNOWN_FLAGS},
    {"fixtures/vtpe_v1/reject/core_reader_on_extended.bin", VTPE_ERR_UNSUPPORTED_PROFILE, VTPE_OK},
    {"fixtures/vtpe_v1/reject/fp32_aux_in_core_profile.bin", VTPE_ERR_PROFILE_SECTION_FORBIDDEN, VTPE_ERR_PROFILE_SECTION_FORBIDDEN},
    {"fixtures/vtpe_v1/reject/malformed_fp32_aux_nan.bin", VTPE_ERR_UNSUPPORTED_PROFILE, VTPE_ERR_MALFORMED_FP32_AUX},
    {"fixtures/vtpe_v1/reject/missing_required_section.bin", VTPE_ERR_UNSUPPORTED_PROFILE, VTPE_ERR_MISSING_REQUIRED_SECTION},
    {"fixtures/vtpe_v1/reject/bad_section_checksum.bin", VTPE_ERR_CHECKSUM_MISMATCH, VTPE_ERR_CHECKSUM_MISMATCH},
    {"fixtures/vtpe_v1/reject/unknown_section_tag.bin", VTPE_ERR_UNKNOWN_TAG, VTPE_ERR_UNKNOWN_TAG},
    {"fixtures/vtpe_v1/reject/reserved_ternary_code_0b11.bin", VTPE_ERR_RESERVED_TERNARY_CODE, VTPE_ERR_RESERVED_TERNARY_CODE},
};

} // namespace

TEST_GROUP(PortfolioConformanceSuiteGroup) {};

TEST(PortfolioConformanceSuiteGroup, AllThreePortfolioConsumersPass12GoldenFixturesAndMatchOracles) {
    STRCMP_EQUAL("setun", vtpe_portfolio_repo_name(VTPE_CONSUMER_SETUN_RTL_SIM));
    STRCMP_EQUAL(
        "veriobjc-inference-appliance-kit",
        vtpe_portfolio_repo_name(VTPE_CONSUMER_KIT_M45_INT8_ORACLE));
    STRCMP_EQUAL("TernaryTape.jl", vtpe_portfolio_repo_name(VTPE_CONSUMER_TERNARY_TAPE_JL));
    STRCMP_EQUAL("unknown", vtpe_portfolio_repo_name(static_cast<enum vtpe_portfolio_consumer>(99)));

    for (size_t i = 0; i < 12; ++i) {
        std::vector<uint8_t> buf = read_tape(kGoldenCorpus[i].path);

        // 1. setun (Core + Extended capable via veriStream runtime slice + LiteX RTL sim)
        struct vtpe_conformance_report rep_setun;
        LONGS_EQUAL(
            1,
            vtpe_portfolio_verify_fixture(
                VTPE_CONSUMER_SETUN_RTL_SIM,
                buf.data(),
                buf.size(),
                kGoldenCorpus[i].expected_extended,
                &rep_setun));
        LONGS_EQUAL(1, rep_setun.passed);
        STRCMP_EQUAL("setun", rep_setun.repo_name);

        // 2. veriobjc-inference-appliance-kit M4.5 INT8 tape oracle (Core-only reader per ADR-0002)
        struct vtpe_conformance_report rep_kit;
        LONGS_EQUAL(
            1,
            vtpe_portfolio_verify_fixture(
                VTPE_CONSUMER_KIT_M45_INT8_ORACLE,
                buf.data(),
                buf.size(),
                kGoldenCorpus[i].expected_core,
                &rep_kit));
        LONGS_EQUAL(1, rep_kit.passed);
        STRCMP_EQUAL("veriobjc-inference-appliance-kit", rep_kit.repo_name);

        // 3. TernaryTape.jl reference interpreter (Extended-capable)
        struct vtpe_conformance_report rep_jl;
        LONGS_EQUAL(
            1,
            vtpe_portfolio_verify_fixture(
                VTPE_CONSUMER_TERNARY_TAPE_JL,
                buf.data(),
                buf.size(),
                kGoldenCorpus[i].expected_extended,
                &rep_jl));
        LONGS_EQUAL(1, rep_jl.passed);
        STRCMP_EQUAL("TernaryTape.jl", rep_jl.repo_name);

        if (kGoldenCorpus[i].expected_core == VTPE_OK) {
            // Bit-exact agreement across all 3 consumers on the golden matmul vector [12, 20, 28, 36]
            LONGS_EQUAL(28, rep_setun.oracle_acc0);
            LONGS_EQUAL(36, rep_setun.oracle_acc1);
            LONGS_EQUAL(7, rep_setun.oracle_scaled0);
            LONGS_EQUAL(9, rep_setun.oracle_scaled1);

            LONGS_EQUAL(rep_setun.oracle_acc0, rep_kit.oracle_acc0);
            LONGS_EQUAL(rep_setun.oracle_acc1, rep_kit.oracle_acc1);
            LONGS_EQUAL(rep_setun.oracle_scaled0, rep_kit.oracle_scaled0);
            LONGS_EQUAL(rep_setun.oracle_scaled1, rep_kit.oracle_scaled1);

            LONGS_EQUAL(rep_setun.oracle_acc0, rep_jl.oracle_acc0);
            LONGS_EQUAL(rep_setun.oracle_acc1, rep_jl.oracle_acc1);
            LONGS_EQUAL(rep_setun.oracle_scaled0, rep_jl.oracle_scaled0);
            LONGS_EQUAL(rep_setun.oracle_scaled1, rep_jl.oracle_scaled1);
        }
    }
}

TEST(PortfolioConformanceSuiteGroup, FailureReportingNamesRepoAndViolatedClause) {
    std::vector<uint8_t> bad_magic = read_tape("fixtures/vtpe_v1/reject/bad_magic.bin");

    // When a consumer expects VTPE_OK but receives a corrupted tape, the report fails
    // and names both the repo and the exact violated clause.
    struct vtpe_conformance_report rep;
    LONGS_EQUAL(
        0,
        vtpe_portfolio_verify_fixture(
            VTPE_CONSUMER_SETUN_RTL_SIM,
            bad_magic.data(),
            bad_magic.size(),
            VTPE_OK,
            &rep));
    LONGS_EQUAL(0, rep.passed);
    STRCMP_EQUAL("setun", rep.repo_name);
    STRCMP_EQUAL("§1.header_magic", rep.violated_clause);

    // Exhaustive clause mapping check across all vtpe_v1_error_t codes
    STRCMP_EQUAL("§0.ok", vtpe_portfolio_clause_for_error(VTPE_OK));
    STRCMP_EQUAL("§1.header_framing", vtpe_portfolio_clause_for_error(VTPE_ERR_NULL_OR_TRUNCATED));
    STRCMP_EQUAL("§1.header_magic", vtpe_portfolio_clause_for_error(VTPE_ERR_BAD_MAGIC));
    STRCMP_EQUAL("§1.version_major", vtpe_portfolio_clause_for_error(VTPE_ERR_UNSUPPORTED_VERSION));
    STRCMP_EQUAL("§1.reserved_zero", vtpe_portfolio_clause_for_error(VTPE_ERR_NONZERO_RESERVED));
    STRCMP_EQUAL("§1.fnv1a32_checksum", vtpe_portfolio_clause_for_error(VTPE_ERR_CHECKSUM_MISMATCH));
    STRCMP_EQUAL("§1.flags_adr0002_reserved_bits", vtpe_portfolio_clause_for_error(VTPE_ERR_UNKNOWN_FLAGS));
    STRCMP_EQUAL("§1.flags_adr0002_profile_bit2", vtpe_portfolio_clause_for_error(VTPE_ERR_UNSUPPORTED_PROFILE));
    STRCMP_EQUAL("§2.section_count", vtpe_portfolio_clause_for_error(VTPE_ERR_SECTION_COUNT_MISMATCH));
    STRCMP_EQUAL("§2.total_length", vtpe_portfolio_clause_for_error(VTPE_ERR_LENGTH_MISMATCH));
    STRCMP_EQUAL("§1.artifact_id_fnv1a64", vtpe_portfolio_clause_for_error(VTPE_ERR_ARTIFACT_ID_MISMATCH));
    STRCMP_EQUAL("§2.unknown_section_tag", vtpe_portfolio_clause_for_error(VTPE_ERR_UNKNOWN_TAG));
    STRCMP_EQUAL("§2.duplicate_section_tag", vtpe_portfolio_clause_for_error(VTPE_ERR_DUPLICATE_TAG));
    STRCMP_EQUAL("§2.profile_section_forbidden", vtpe_portfolio_clause_for_error(VTPE_ERR_PROFILE_SECTION_FORBIDDEN));
    STRCMP_EQUAL("§2.missing_mandatory_section", vtpe_portfolio_clause_for_error(VTPE_ERR_MISSING_REQUIRED_SECTION));
    STRCMP_EQUAL("§6.arch_descriptor", vtpe_portfolio_clause_for_error(VTPE_ERR_MALFORMED_ARCH));
    STRCMP_EQUAL("§3.dim_payload_mismatch", vtpe_portfolio_clause_for_error(VTPE_ERR_DIM_PAYLOAD_MISMATCH));
    STRCMP_EQUAL("§3.reserved_ternary_code_0b11", vtpe_portfolio_clause_for_error(VTPE_ERR_RESERVED_TERNARY_CODE));
    STRCMP_EQUAL("§3.nonzero_tail_padding", vtpe_portfolio_clause_for_error(VTPE_ERR_NONZERO_PADDING));
    STRCMP_EQUAL("§5.invalid_scale_exponent", vtpe_portfolio_clause_for_error(VTPE_ERR_INVALID_SCALE));
    STRCMP_EQUAL("§7.manifest_schema", vtpe_portfolio_clause_for_error(VTPE_ERR_MALFORMED_MANIFEST));
    STRCMP_EQUAL("§4.self_relative_link_bounds", vtpe_portfolio_clause_for_error(VTPE_ERR_INVALID_LINK));
    STRCMP_EQUAL("§6.fp32_aux_schema", vtpe_portfolio_clause_for_error(VTPE_ERR_MALFORMED_FP32_AUX));
    STRCMP_EQUAL("§8.unknown_error", vtpe_portfolio_clause_for_error(static_cast<vtpe_v1_error_t>(999)));

    // Defensive null/invalid consumer branches
    LONGS_EQUAL(
        0,
        vtpe_portfolio_verify_fixture(
            VTPE_CONSUMER_SETUN_RTL_SIM, bad_magic.data(), bad_magic.size(), VTPE_OK, nullptr));
    LONGS_EQUAL(
        0,
        vtpe_portfolio_verify_fixture(
            static_cast<enum vtpe_portfolio_consumer>(99),
            bad_magic.data(),
            bad_magic.size(),
            VTPE_OK,
            &rep));
}
