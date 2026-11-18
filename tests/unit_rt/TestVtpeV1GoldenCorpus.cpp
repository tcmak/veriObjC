#include "CppUTest/TestHarness.h"
#include "veriobjc_runtime.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef VERIOBJC_SOURCE_DIR
#define VERIOBJC_SOURCE_DIR "."
#endif

static std::vector<uint8_t> load_fixture_bytes(const char *rel_path) {
    std::string full_path = std::string(VERIOBJC_SOURCE_DIR) + "/fixtures/vtpe_v1/" + rel_path;
    FILE *fp = std::fopen(full_path.c_str(), "rb");
    CHECK_TEXT(fp != nullptr, full_path.c_str());
    std::fseek(fp, 0, SEEK_END);
    long sz = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    CHECK_TRUE(sz > 0);
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    size_t n = std::fread(buf.data(), 1u, buf.size(), fp);
    std::fclose(fp);
    LONGS_EQUAL(buf.size(), n);
    return buf;
}

TEST_GROUP(VtpeV1GoldenCorpusGroup) {
};

TEST(VtpeV1GoldenCorpusGroup, AcceptFixturesPassUnderTargetProfile) {
    auto core_min = load_fixture_bytes("accept/core_minimal_dot4.bin");
    LONGS_EQUAL(VTPE_OK, vtpe_v1_validate_tape(core_min.data(), core_min.size(), VTPE_PROFILE_CORE));
    LONGS_EQUAL(VTPE_OK, vtpe_v1_validate_tape(core_min.data(), core_min.size(), VTPE_PROFILE_EXTENDED));

    auto core_full = load_fixture_bytes("accept/core_with_manifest_and_links.bin");
    LONGS_EQUAL(VTPE_OK, vtpe_v1_validate_tape(core_full.data(), core_full.size(), VTPE_PROFILE_CORE));

    auto ext_full = load_fixture_bytes("accept/extended_with_fp32_aux.bin");
    LONGS_EQUAL(VTPE_OK, vtpe_v1_validate_tape(ext_full.data(), ext_full.size(), VTPE_PROFILE_EXTENDED));
}

TEST(VtpeV1GoldenCorpusGroup, RejectFixturesProduceExactNamedDiagnostics) {
    auto bad_magic = load_fixture_bytes("reject/bad_magic.bin");
    LONGS_EQUAL(VTPE_ERR_BAD_MAGIC, vtpe_v1_validate_tape(bad_magic.data(), bad_magic.size(), VTPE_PROFILE_CORE));

    auto unk_flags = load_fixture_bytes("reject/unknown_flags_bit3.bin");
    LONGS_EQUAL(VTPE_ERR_UNKNOWN_FLAGS, vtpe_v1_validate_tape(unk_flags.data(), unk_flags.size(), VTPE_PROFILE_CORE));

    auto core_on_ext = load_fixture_bytes("reject/core_reader_on_extended.bin");
    LONGS_EQUAL(VTPE_ERR_UNSUPPORTED_PROFILE, vtpe_v1_validate_tape(core_on_ext.data(), core_on_ext.size(), VTPE_PROFILE_CORE));

    auto fp32_in_core = load_fixture_bytes("reject/fp32_aux_in_core_profile.bin");
    LONGS_EQUAL(VTPE_ERR_PROFILE_SECTION_FORBIDDEN, vtpe_v1_validate_tape(fp32_in_core.data(), fp32_in_core.size(), VTPE_PROFILE_EXTENDED));

    auto nan_fp32 = load_fixture_bytes("reject/malformed_fp32_aux_nan.bin");
    LONGS_EQUAL(VTPE_ERR_MALFORMED_FP32_AUX, vtpe_v1_validate_tape(nan_fp32.data(), nan_fp32.size(), VTPE_PROFILE_EXTENDED));

    auto missing_sec = load_fixture_bytes("reject/missing_required_section.bin");
    LONGS_EQUAL(VTPE_ERR_MISSING_REQUIRED_SECTION, vtpe_v1_validate_tape(missing_sec.data(), missing_sec.size(), VTPE_PROFILE_EXTENDED));

    auto bad_sec_crc = load_fixture_bytes("reject/bad_section_checksum.bin");
    LONGS_EQUAL(VTPE_ERR_CHECKSUM_MISMATCH, vtpe_v1_validate_tape(bad_sec_crc.data(), bad_sec_crc.size(), VTPE_PROFILE_CORE));

    auto unk_tag = load_fixture_bytes("reject/unknown_section_tag.bin");
    LONGS_EQUAL(VTPE_ERR_UNKNOWN_TAG, vtpe_v1_validate_tape(unk_tag.data(), unk_tag.size(), VTPE_PROFILE_CORE));

    auto res_code = load_fixture_bytes("reject/reserved_ternary_code_0b11.bin");
    LONGS_EQUAL(VTPE_ERR_RESERVED_TERNARY_CODE, vtpe_v1_validate_tape(res_code.data(), res_code.size(), VTPE_PROFILE_CORE));
}

TEST(VtpeV1GoldenCorpusGroup, ExhaustivePerByteMutationFlipsAcceptToReject) {
    // §8 invariant: every single byte index of a valid reference tape must have
    // a mutation that flips accept -> reject (including artifact_id at 0x10..0x17).
    const char *accept_files[] = {
        "accept/core_minimal_dot4.bin",
        "accept/core_with_manifest_and_links.bin",
        "accept/extended_with_fp32_aux.bin"
    };
    for (const char *rel : accept_files) {
        auto orig = load_fixture_bytes(rel);
        LONGS_EQUAL(VTPE_OK, vtpe_v1_validate_tape(orig.data(), orig.size(), VTPE_PROFILE_EXTENDED));
        for (size_t idx = 0u; idx < orig.size(); ++idx) {
            std::vector<uint8_t> mutated = orig;
            mutated[idx] ^= 0x01u;
            vtpe_v1_error_t err = vtpe_v1_validate_tape(mutated.data(), mutated.size(), VTPE_PROFILE_EXTENDED);
            CHECK_TRUE(err != VTPE_OK);
        }
    }
}
