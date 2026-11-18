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

static void recompute_tape_checksums(std::vector<uint8_t> &tape, bool recompute_artifact_id = true, bool recompute_hdr_crc = true) {
    if (tape.size() < sizeof(struct vtpe_v1_header)) return;
    struct vtpe_v1_header hdr;
    std::memcpy(&hdr, tape.data(), sizeof(hdr));
    if (recompute_artifact_id && tape.size() >= sizeof(hdr) + hdr.total_length) {
        hdr.artifact_id = vtpe_v1_fnv1a64(tape.data() + sizeof(hdr), hdr.total_length);
        std::memcpy(tape.data(), &hdr, sizeof(hdr));
    }
    if (recompute_hdr_crc) {
        hdr.header_fnv1a = vtpe_v1_header_fnv1a32(tape.data());
        std::memcpy(tape.data(), &hdr, sizeof(hdr));
    }
}

static void append_u16(std::vector<uint8_t> &v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFFu));
    v.push_back(static_cast<uint8_t>((x >> 8u) & 0xFFu));
}

static void append_u32(std::vector<uint8_t> &v, uint32_t x) {
    for (unsigned i = 0; i < 4u; ++i) v.push_back(static_cast<uint8_t>((x >> (8u * i)) & 0xFFu));
}

static void append_section(std::vector<uint8_t> &secs, uint16_t tag, const std::vector<uint8_t> &payload, uint16_t reserved = 0u) {
    struct vtpe_v1_section_header sh = {
        tag, reserved, static_cast<uint32_t>(payload.size()), vtpe_v1_fnv1a32(payload.data(), payload.size())
    };
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&sh);
    secs.insert(secs.end(), p, p + sizeof(sh));
    secs.insert(secs.end(), payload.begin(), payload.end());
}

static std::vector<uint8_t> wrap_tape(const std::vector<uint8_t> &secs, uint8_t sec_count, uint8_t flags = 0x00u) {
    struct vtpe_v1_header hdr = {};
    hdr.magic = VTPE_V1_MAGIC;
    hdr.version_major = 1u;
    hdr.version_minor = 0u;
    hdr.flags = flags;
    hdr.section_count = sec_count;
    hdr.total_length = static_cast<uint32_t>(secs.size());
    std::vector<uint8_t> out(sizeof(hdr));
    std::memcpy(out.data(), &hdr, sizeof(hdr));
    out.insert(out.end(), secs.begin(), secs.end());
    recompute_tape_checksums(out);
    return out;
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

TEST(VtpeV1GoldenCorpusGroup, HeaderAndSectionFramingEdgeBranches) {
    auto base_tape = load_fixture_bytes("accept/core_minimal_dot4.bin");

    // 1. Null or truncated
    LONGS_EQUAL(VTPE_ERR_NULL_OR_TRUNCATED, vtpe_v1_validate_tape(nullptr, 64u, VTPE_PROFILE_CORE));
    LONGS_EQUAL(VTPE_ERR_NULL_OR_TRUNCATED, vtpe_v1_validate_tape(base_tape.data(), 16u, VTPE_PROFILE_CORE));

    // 2. Unsupported major version
    {
        auto t = base_tape;
        t[4] = 2u;
        LONGS_EQUAL(VTPE_ERR_UNSUPPORTED_VERSION, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }

    // 3. Nonzero header reserved
    {
        auto t = base_tape;
        t[24] = 1u;
        LONGS_EQUAL(VTPE_ERR_NONZERO_RESERVED, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }

    // 4. section_count < 3
    {
        auto t = base_tape;
        t[7] = 2u;
        recompute_tape_checksums(t, false, true);
        LONGS_EQUAL(VTPE_ERR_SECTION_COUNT_MISMATCH, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }

    // 5. total_length != in_len - 32
    {
        auto t = base_tape;
        t.push_back(0u);
        LONGS_EQUAL(VTPE_ERR_LENGTH_MISMATCH, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }

    // 6. artifact_id mismatch (with valid header checksum)
    {
        auto t = base_tape;
        t[16] ^= 0xFFu;
        recompute_tape_checksums(t, false, true);
        LONGS_EQUAL(VTPE_ERR_ARTIFACT_ID_MISMATCH, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }

    // 7. Section walk: not enough bytes for section header, section reserved != 0, sh.length > rem, offset != total_length
    {
        auto t = base_tape;
        t[7] = 4u; // claims 4 sections, 4th section header truncated
        recompute_tape_checksums(t);
        LONGS_EQUAL(VTPE_ERR_LENGTH_MISMATCH, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }
    {
        auto t = base_tape;
        t[32 + 2] = 1u; // section 0 reserved != 0
        recompute_tape_checksums(t);
        LONGS_EQUAL(VTPE_ERR_NONZERO_RESERVED, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }
    {
        auto t = base_tape;
        uint32_t huge_len = 9999u;
        std::memcpy(t.data() + 32 + 4, &huge_len, 4u);
        recompute_tape_checksums(t);
        LONGS_EQUAL(VTPE_ERR_LENGTH_MISMATCH, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }
    {
        // Trailing unconsumed byte after 3 sections
        std::vector<uint8_t> secs(base_tape.begin() + 32, base_tape.end());
        secs.push_back(0xAAu);
        auto t = wrap_tape(secs, 3u, 0x00u);
        LONGS_EQUAL(VTPE_ERR_LENGTH_MISMATCH, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }

    // 8. Tag 0 (< VTPE_V1_TAG_WEIGHTS) and duplicate tag
    {
        auto t = base_tape;
        uint16_t zero_tag = 0u;
        std::memcpy(t.data() + 32, &zero_tag, 2u);
        recompute_tape_checksums(t);
        LONGS_EQUAL(VTPE_ERR_UNKNOWN_TAG, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }
    {
        auto t = base_tape;
        uint16_t dup_tag = VTPE_V1_TAG_WEIGHTS;
        // Overwrite section 1 tag (at 32 + 12 + 2 = 46) with 0x0001
        std::memcpy(t.data() + 46, &dup_tag, 2u);
        recompute_tape_checksums(t);
        LONGS_EQUAL(VTPE_ERR_DUPLICATE_TAG, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }

    // 9. Missing base sections / manifest flag mismatch / links flag mismatch
    {
        // Replace tag 0x0003 with 0x0004 while flags=0x02 (missing 0x0003)
        auto t = base_tape;
        uint16_t man_tag = VTPE_V1_TAG_MANIFEST;
        std::memcpy(t.data() + 32 + 14 + 14, &man_tag, 2u);
        t[6] = VTPE_V1_FLAG_MANIFEST;
        recompute_tape_checksums(t);
        LONGS_EQUAL(VTPE_ERR_MISSING_REQUIRED_SECTION, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }
    {
        // Replace tag 0x0002 with 0x0004 (missing 0x0002)
        auto t = base_tape;
        uint16_t man_tag = VTPE_V1_TAG_MANIFEST;
        std::memcpy(t.data() + 32 + 14, &man_tag, 2u);
        t[6] = VTPE_V1_FLAG_MANIFEST;
        recompute_tape_checksums(t);
        LONGS_EQUAL(VTPE_ERR_MISSING_REQUIRED_SECTION, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }
    {
        // Replace tag 0x0001 with 0x0004 (missing 0x0001)
        auto t = base_tape;
        uint16_t man_tag = VTPE_V1_TAG_MANIFEST;
        std::memcpy(t.data() + 32, &man_tag, 2u);
        t[6] = VTPE_V1_FLAG_MANIFEST;
        recompute_tape_checksums(t);
        LONGS_EQUAL(VTPE_ERR_MISSING_REQUIRED_SECTION, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }
    {
        auto t = base_tape;
        t[6] = VTPE_V1_FLAG_MANIFEST; // manifest flag set, no 0x0004 section
        recompute_tape_checksums(t);
        LONGS_EQUAL(VTPE_ERR_MISSING_REQUIRED_SECTION, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }
    {
        auto t = base_tape;
        t[6] = VTPE_V1_FLAG_LINKS; // links flag set, no 0x0005 section
        recompute_tape_checksums(t);
        LONGS_EQUAL(VTPE_ERR_MISSING_REQUIRED_SECTION, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }
}

TEST(VtpeV1GoldenCorpusGroup, PayloadValidatorAllBranches) {
    // Helper to build a 3-section Core tape from raw payloads
    auto build3 = [](const std::vector<uint8_t> &w, const std::vector<uint8_t> &sc, const std::vector<uint8_t> &ar, uint8_t flags = 0u) {
        std::vector<uint8_t> secs;
        append_section(secs, VTPE_V1_TAG_WEIGHTS, w);
        append_section(secs, VTPE_V1_TAG_SCALES, sc);
        append_section(secs, VTPE_V1_TAG_ARCH_DESCRIPTOR, ar);
        return wrap_tape(secs, 3u, flags);
    };

    std::vector<uint8_t> good_w = {0x49u, 0x16u};
    std::vector<uint8_t> good_sc = {0xFEu, 0xFFu}; // -2
    auto make_arch1 = [](uint16_t kind, uint16_t din, uint16_t dout, uint8_t stages, uint8_t res, uint32_t lat, uint32_t cap, uint16_t n_layers = 1u, uint16_t hdr_res = 0u) {
        std::vector<uint8_t> ar;
        append_u16(ar, n_layers);
        append_u16(ar, hdr_res);
        struct vtpe_v1_arch_layer L = {kind, din, dout, stages, res, lat, cap};
        const uint8_t *p = reinterpret_cast<const uint8_t *>(&L);
        ar.insert(ar.end(), p, p + sizeof(L));
        return ar;
    };

    // Arch < 4 bytes, arch_res != 0, n_layers == 0, arch_len mismatch
    {
        auto t = build3(good_w, good_sc, {1u, 0u});
        LONGS_EQUAL(VTPE_ERR_MALFORMED_ARCH, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }
    {
        auto t = build3(good_w, good_sc, make_arch1(4, 4, 2, 2, 0, 2, 16, 1, 1));
        LONGS_EQUAL(VTPE_ERR_NONZERO_RESERVED, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }
    {
        auto t = build3(good_w, good_sc, {0u, 0u, 0u, 0u});
        LONGS_EQUAL(VTPE_ERR_MALFORMED_ARCH, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }
    {
        auto t = build3(good_w, good_sc, make_arch1(4, 4, 2, 2, 0, 2, 16, 2, 0));
        LONGS_EQUAL(VTPE_ERR_MALFORMED_ARCH, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }

    // Layer reserved != 0, bad layer_kind (0, 8), dim_in=0, dim_out=0, context_cap=0
    {
        auto t = build3(good_w, good_sc, make_arch1(4, 4, 2, 2, 1, 2, 16));
        LONGS_EQUAL(VTPE_ERR_NONZERO_RESERVED, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }
    {
        auto t = build3(good_w, good_sc, make_arch1(0, 4, 2, 2, 0, 2, 16));
        LONGS_EQUAL(VTPE_ERR_MALFORMED_ARCH, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }
    {
        auto t = build3(good_w, good_sc, make_arch1(8, 4, 2, 2, 0, 2, 16));
        LONGS_EQUAL(VTPE_ERR_MALFORMED_ARCH, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }
    {
        auto t = build3(good_w, good_sc, make_arch1(4, 0, 2, 2, 0, 2, 16));
        LONGS_EQUAL(VTPE_ERR_MALFORMED_ARCH, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }
    {
        auto t = build3(good_w, good_sc, make_arch1(4, 4, 0, 2, 0, 2, 16));
        LONGS_EQUAL(VTPE_ERR_MALFORMED_ARCH, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }
    {
        auto t = build3(good_w, good_sc, make_arch1(4, 4, 2, 2, 0, 2, 0));
        LONGS_EQUAL(VTPE_ERR_MALFORMED_ARCH, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }

    // Extended layer_kind=6 in Core tape
    {
        auto t = build3(good_w, good_sc, make_arch1(6, 4, 2, 0, 0, 2, 16));
        LONGS_EQUAL(VTPE_ERR_PROFILE_SECTION_FORBIDDEN, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }

    // reduce_stages mismatch, latency < reduce_stages, non-dense layer with reduce_stages != 0
    {
        auto t = build3(good_w, good_sc, make_arch1(4, 4, 2, 3, 0, 3, 16));
        LONGS_EQUAL(VTPE_ERR_MALFORMED_ARCH, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }
    {
        auto t = build3(good_w, good_sc, make_arch1(4, 4, 2, 2, 0, 1, 16));
        LONGS_EQUAL(VTPE_ERR_MALFORMED_ARCH, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }
    {
        auto t = build3(good_w, good_sc, make_arch1(1, 4, 4, 1, 0, 1, 16));
        LONGS_EQUAL(VTPE_ERR_MALFORMED_ARCH, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }

    // num_dense_layers == 0, weights_len != expected, scales_len != expected
    {
        auto t = build3(good_w, good_sc, make_arch1(1, 4, 4, 0, 0, 1, 16));
        LONGS_EQUAL(VTPE_ERR_DIM_PAYLOAD_MISMATCH, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }
    {
        auto t = build3({0x49u}, good_sc, make_arch1(4, 4, 2, 2, 0, 2, 16));
        LONGS_EQUAL(VTPE_ERR_DIM_PAYLOAD_MISMATCH, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }
    {
        auto t = build3(good_w, {0xFEu, 0xFFu, 0x00u, 0x00u}, make_arch1(4, 4, 2, 2, 0, 2, 16));
        LONGS_EQUAL(VTPE_ERR_DIM_PAYLOAD_MISMATCH, vtpe_v1_validate_tape(t.data(), t.size(), VTPE_PROFILE_CORE));
    }

    // Non-zero padding lane (dim_in=3, row_bytes=1, lane 3 must be 0)
    {
        // Valid padding (lane 3 = 0):
        auto t_ok = build3({0x01u}, good_sc, make_arch1(4, 3, 1, 2, 0, 2, 16));
        LONGS_EQUAL(VTPE_OK, vtpe_v1_validate_tape(t_ok.data(), t_ok.size(), VTPE_PROFILE_CORE));
        // Non-zero padding (lane 3 = 0b01 -> 0x41):
        auto t_bad = build3({0x41u}, good_sc, make_arch1(4, 3, 1, 2, 0, 2, 16));
        LONGS_EQUAL(VTPE_ERR_NONZERO_PADDING, vtpe_v1_validate_tape(t_bad.data(), t_bad.size(), VTPE_PROFILE_CORE));
    }

    // Invalid scale (< -30 and > 30)
    {
        auto t_lo = build3(good_w, {0xE1u, 0xFFu}, make_arch1(4, 4, 2, 2, 0, 2, 16)); // -31
        LONGS_EQUAL(VTPE_ERR_INVALID_SCALE, vtpe_v1_validate_tape(t_lo.data(), t_lo.size(), VTPE_PROFILE_CORE));
        auto t_hi = build3(good_w, {31u, 0u}, make_arch1(4, 4, 2, 2, 0, 2, 16)); // +31
        LONGS_EQUAL(VTPE_ERR_INVALID_SCALE, vtpe_v1_validate_tape(t_hi.data(), t_hi.size(), VTPE_PROFILE_CORE));
    }

    // Manifest error branches
    {
        uint16_t n_layers = 0u;
        LONGS_EQUAL(VTPE_OK, vtpe_v1_validate_arch_weights_scales(
            false, make_arch1(4, 4, 2, 2, 0, 2, 16).data(), 20u, good_w.data(), 2u, good_sc.data(), 2u, &n_layers));

        // Truncated before 5 fields
        uint8_t short_man[1] = {0u};
        LONGS_EQUAL(VTPE_ERR_MALFORMED_MANIFEST, vtpe_v1_validate_manifest(short_man, 1u));

        // flen == 0
        uint8_t zero_flen[2] = {0u, 0u};
        LONGS_EQUAL(VTPE_ERR_MALFORMED_MANIFEST, vtpe_v1_validate_manifest(zero_flen, 2u));

        // flen > remaining
        uint8_t big_flen[2] = {10u, 0u};
        LONGS_EQUAL(VTPE_ERR_MALFORMED_MANIFEST, vtpe_v1_validate_manifest(big_flen, 2u));

        // f == 1 && flen != 32
        std::vector<uint8_t> bad_run_id = {1u, 0u, 'a', 4u, 0u, 'r', 'u', 'n', '1'};
        LONGS_EQUAL(VTPE_ERR_MALFORMED_MANIFEST, vtpe_v1_validate_manifest(bad_run_id.data(), static_cast<uint32_t>(bad_run_id.size())));

        // f == 4 && flen < 8, and trailing bytes after 5 fields
        std::vector<uint8_t> m_short_hash;
        append_u16(m_short_hash, 1u); m_short_hash.push_back('p');
        append_u16(m_short_hash, 32u); m_short_hash.insert(m_short_hash.end(), 32u, 'r');
        append_u16(m_short_hash, 1u); m_short_hash.push_back('q');
        append_u16(m_short_hash, 1u); m_short_hash.push_back('c');
        append_u16(m_short_hash, 4u); m_short_hash.insert(m_short_hash.end(), 4u, 'h');
        LONGS_EQUAL(VTPE_ERR_MALFORMED_MANIFEST, vtpe_v1_validate_manifest(m_short_hash.data(), static_cast<uint32_t>(m_short_hash.size())));

        // Trailing extra byte in full tape with manifest
        std::vector<uint8_t> m_trailing = m_short_hash;
        m_trailing[m_trailing.size() - 6u] = 8u; // make 5th field len=8
        m_trailing.insert(m_trailing.end(), 4u, 'h');
        m_trailing.push_back('X'); // extra trailing byte
        std::vector<uint8_t> secs;
        append_section(secs, VTPE_V1_TAG_WEIGHTS, good_w);
        append_section(secs, VTPE_V1_TAG_SCALES, good_sc);
        append_section(secs, VTPE_V1_TAG_ARCH_DESCRIPTOR, make_arch1(4, 4, 2, 2, 0, 2, 16));
        append_section(secs, VTPE_V1_TAG_MANIFEST, m_trailing);
        auto t_man = wrap_tape(secs, 4u, VTPE_V1_FLAG_MANIFEST);
        LONGS_EQUAL(VTPE_ERR_MALFORMED_MANIFEST, vtpe_v1_validate_tape(t_man.data(), t_man.size(), VTPE_PROFILE_CORE));
    }

    // Link table error branches (len=0, len%4!=0, delta=0, delta%4!=0, target<0, target>=len)
    {
        uint8_t dummy = 0u;
        LONGS_EQUAL(VTPE_ERR_INVALID_LINK, vtpe_v1_validate_links(&dummy, 0u));
        LONGS_EQUAL(VTPE_ERR_INVALID_LINK, vtpe_v1_validate_links(&dummy, 2u));

        int32_t d_zero = 0;
        LONGS_EQUAL(VTPE_ERR_INVALID_LINK, vtpe_v1_validate_links(reinterpret_cast<const uint8_t *>(&d_zero), 4u));
        int32_t d_unaligned = 2;
        LONGS_EQUAL(VTPE_ERR_INVALID_LINK, vtpe_v1_validate_links(reinterpret_cast<const uint8_t *>(&d_unaligned), 8u));
        int32_t d_neg = -4;
        LONGS_EQUAL(VTPE_ERR_INVALID_LINK, vtpe_v1_validate_links(reinterpret_cast<const uint8_t *>(&d_neg), 4u));

        std::vector<uint8_t> bad_link_payload(4u, 0u);
        int32_t d_oob = 8;
        std::memcpy(bad_link_payload.data(), &d_oob, 4u);
        std::vector<uint8_t> secs;
        append_section(secs, VTPE_V1_TAG_WEIGHTS, good_w);
        append_section(secs, VTPE_V1_TAG_SCALES, good_sc);
        append_section(secs, VTPE_V1_TAG_ARCH_DESCRIPTOR, make_arch1(4, 4, 2, 2, 0, 2, 16));
        append_section(secs, VTPE_V1_TAG_LINK_TABLE, bad_link_payload);
        auto t_lnk = wrap_tape(secs, 4u, VTPE_V1_FLAG_LINKS);
        LONGS_EQUAL(VTPE_ERR_INVALID_LINK, vtpe_v1_validate_tape(t_lnk.data(), t_lnk.size(), VTPE_PROFILE_CORE));
    }

    // fp32_aux error branches and aux_role == 3 (host_softmax_head, layer_kind == 7)
    {
        auto arch2 = make_arch1(4, 4, 2, 2, 0, 2, 16, 2, 0);
        struct vtpe_v1_arch_layer L_soft = {7u, 2u, 2u, 0u, 0u, 1u, 16u};
        const uint8_t *ps = reinterpret_cast<const uint8_t *>(&L_soft);
        arch2.insert(arch2.end(), ps, ps + sizeof(L_soft));

        uint8_t short_aux[2] = {0u, 0u};
        LONGS_EQUAL(VTPE_ERR_MALFORMED_FP32_AUX, vtpe_v1_validate_fp32_aux(short_aux, 2u, arch2.data(), 2u));

        uint8_t res_aux[4] = {1u, 0u, 1u, 0u};
        LONGS_EQUAL(VTPE_ERR_NONZERO_RESERVED, vtpe_v1_validate_fp32_aux(res_aux, 4u, arch2.data(), 2u));

        uint8_t zero_cnt[4] = {0u, 0u, 0u, 0u};
        LONGS_EQUAL(VTPE_ERR_MALFORMED_FP32_AUX, vtpe_v1_validate_fp32_aux(zero_cnt, 4u, arch2.data(), 2u));

        uint8_t trunc_rec[6] = {1u, 0u, 0u, 0u, 0u, 0u};
        LONGS_EQUAL(VTPE_ERR_MALFORMED_FP32_AUX, vtpe_v1_validate_fp32_aux(trunc_rec, 6u, arch2.data(), 2u));

        // rec.reserved != 0
        std::vector<uint8_t> rec_res = {1u, 0u, 0u, 0u, 1u, 0u, 3u, 1u, 2u, 0u, 0u, 0u};
        LONGS_EQUAL(VTPE_ERR_NONZERO_RESERVED, vtpe_v1_validate_fp32_aux(rec_res.data(), static_cast<uint32_t>(rec_res.size()), arch2.data(), 2u));

        // layer_index >= n_layers, aux_role == 0, aux_role == 4, elem_count > remaining
        std::vector<uint8_t> bad_lidx = {1u, 0u, 0u, 0u, 5u, 0u, 3u, 0u, 2u, 0u, 0u, 0u};
        LONGS_EQUAL(VTPE_ERR_MALFORMED_FP32_AUX, vtpe_v1_validate_fp32_aux(bad_lidx.data(), static_cast<uint32_t>(bad_lidx.size()), arch2.data(), 2u));

        std::vector<uint8_t> bad_role0 = {1u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 2u, 0u, 0u, 0u};
        LONGS_EQUAL(VTPE_ERR_MALFORMED_FP32_AUX, vtpe_v1_validate_fp32_aux(bad_role0.data(), static_cast<uint32_t>(bad_role0.size()), arch2.data(), 2u));

        std::vector<uint8_t> bad_role4 = {1u, 0u, 0u, 0u, 1u, 0u, 4u, 0u, 2u, 0u, 0u, 0u};
        LONGS_EQUAL(VTPE_ERR_MALFORMED_FP32_AUX, vtpe_v1_validate_fp32_aux(bad_role4.data(), static_cast<uint32_t>(bad_role4.size()), arch2.data(), 2u));

        std::vector<uint8_t> big_elems = {1u, 0u, 0u, 0u, 1u, 0u, 3u, 0u, 99u, 0u, 0u, 0u};
        LONGS_EQUAL(VTPE_ERR_MALFORMED_FP32_AUX, vtpe_v1_validate_fp32_aux(big_elems.data(), static_cast<uint32_t>(big_elems.size()), arch2.data(), 2u));

        // layer_kind != exp_kind (role 3 targeting layer 0 kind 4) and elem_count != exp_elems
        std::vector<uint8_t> wrong_kind = {1u, 0u, 0u, 0u, 0u, 0u, 3u, 0u, 2u, 0u, 0u, 0u};
        wrong_kind.insert(wrong_kind.end(), 8u, 0u);
        LONGS_EQUAL(VTPE_ERR_MALFORMED_FP32_AUX, vtpe_v1_validate_fp32_aux(wrong_kind.data(), static_cast<uint32_t>(wrong_kind.size()), arch2.data(), 2u));

        std::vector<uint8_t> wrong_elems = {1u, 0u, 0u, 0u, 1u, 0u, 3u, 0u, 1u, 0u, 0u, 0u};
        wrong_elems.insert(wrong_elems.end(), 4u, 0u);
        LONGS_EQUAL(VTPE_ERR_MALFORMED_FP32_AUX, vtpe_v1_validate_fp32_aux(wrong_elems.data(), static_cast<uint32_t>(wrong_elems.size()), arch2.data(), 2u));

        // Valid role 3 (host_softmax_head, 2 floats = 8 bytes), then test trailing byte
        std::vector<uint8_t> valid_role3 = {1u, 0u, 0u, 0u, 1u, 0u, 3u, 0u, 2u, 0u, 0u, 0u};
        valid_role3.insert(valid_role3.end(), 8u, 0u);
        LONGS_EQUAL(VTPE_OK, vtpe_v1_validate_fp32_aux(valid_role3.data(), static_cast<uint32_t>(valid_role3.size()), arch2.data(), 2u));

        valid_role3.push_back(0u);
        LONGS_EQUAL(VTPE_ERR_MALFORMED_FP32_AUX, vtpe_v1_validate_fp32_aux(valid_role3.data(), static_cast<uint32_t>(valid_role3.size()), arch2.data(), 2u));
    }
}
