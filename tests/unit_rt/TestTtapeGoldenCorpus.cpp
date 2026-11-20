#include "CppUTest/TestHarness.h"
#include "vstd/ttape_reader.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#ifndef VERIOBJC_SOURCE_DIR
#define VERIOBJC_SOURCE_DIR "."
#endif

static std::vector<uint8_t> load_fixture_bytes(const char *rel_path) {
    std::string full_path =
        std::string(VERIOBJC_SOURCE_DIR) + "/third_party/TernaryTape/fixtures/" + rel_path;
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

static void recompute_tape_checksums(
    std::vector<uint8_t> &tape,
    bool recompute_artifact_id = true,
    bool recompute_hdr_crc = true
) {
    if (tape.size() < sizeof(struct ttape_v1_header)) {
        return;
    }
    struct ttape_v1_header hdr;
    std::memcpy(&hdr, tape.data(), sizeof(hdr));
    if (recompute_artifact_id && tape.size() >= sizeof(hdr) + hdr.total_length) {
        hdr.artifact_id = ttape_v1_fnv1a64(tape.data() + sizeof(hdr), hdr.total_length);
        std::memcpy(tape.data(), &hdr, sizeof(hdr));
    }
    if (recompute_hdr_crc) {
        hdr.header_fnv1a = ttape_v1_header_fnv1a32(tape.data());
        std::memcpy(tape.data(), &hdr, sizeof(hdr));
    }
}

static void append_u16(std::vector<uint8_t> &v, uint16_t x) {
    v.push_back(static_cast<uint8_t>(x & 0xFFu));
    v.push_back(static_cast<uint8_t>((x >> 8u) & 0xFFu));
}

static void append_section(
    std::vector<uint8_t> &secs,
    uint16_t tag,
    const std::vector<uint8_t> &payload,
    uint16_t reserved = 0u
) {
    struct ttape_v1_section_header sh = {
        tag,
        reserved,
        static_cast<uint32_t>(payload.size()),
        ttape_v1_fnv1a32(payload.data(), payload.size())
    };
    const uint8_t *p = reinterpret_cast<const uint8_t *>(&sh);
    secs.insert(secs.end(), p, p + sizeof(sh));
    secs.insert(secs.end(), payload.begin(), payload.end());
}

static std::vector<uint8_t> wrap_tape(
    const std::vector<uint8_t> &secs,
    uint8_t sec_count,
    uint8_t flags = 0x00u
) {
    struct ttape_v1_header hdr = {};
    hdr.magic = TTAPE_V1_MAGIC;
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

TEST_GROUP(TtapeGoldenCorpusGroup) {};

TEST(TtapeGoldenCorpusGroup, AllFourAcceptFixturesPassUnderDeclaredProfiles) {
    auto core_min = load_fixture_bytes("accept/core_minimal_dot4.ttape");
    LONGS_EQUAL(TTAPE_OK, ttape_v1_validate_tape(core_min.data(), core_min.size(), TTAPE_PROFILE_CORE));
    LONGS_EQUAL(TTAPE_OK, ttape_v1_validate_tape(core_min.data(), core_min.size(), TTAPE_PROFILE_GROUPED));
    LONGS_EQUAL(TTAPE_OK, ttape_v1_validate_tape(core_min.data(), core_min.size(), TTAPE_PROFILE_EXTENDED));

    auto core_full = load_fixture_bytes("accept/core_with_manifest_and_links.ttape");
    LONGS_EQUAL(TTAPE_OK, ttape_v1_validate_tape(core_full.data(), core_full.size(), TTAPE_PROFILE_CORE));

    auto grp_u16 = load_fixture_bytes("accept/grouped_with_u16_scales.ttape");
    LONGS_EQUAL(TTAPE_OK, ttape_v1_validate_tape(grp_u16.data(), grp_u16.size(), TTAPE_PROFILE_GROUPED));
    LONGS_EQUAL(TTAPE_OK, ttape_v1_validate_tape(grp_u16.data(), grp_u16.size(), TTAPE_PROFILE_EXTENDED));

    struct ttape_reader grp_reader;
    LONGS_EQUAL(TTAPE_OK, ttape_reader_open(&grp_reader, grp_u16.data(), grp_u16.size(), TTAPE_PROFILE_GROUPED));
    LONGS_EQUAL(TTAPE_PROFILE_GROUPED, grp_reader.profile);
    LONGS_EQUAL(1u, grp_reader.group_scales_count);
    struct ttape_group_scales_view gview;
    LONGS_EQUAL(1, ttape_reader_get_group_scales(&grp_reader, 0u, &gview));
    LONGS_EQUAL(0u, gview.layer_index);
    LONGS_EQUAL(64u, gview.group_size);
    LONGS_EQUAL(8u, gview.shift);
    LONGS_EQUAL(1u, gview.n_groups);
    uint16_t scale0 = 0u;
    LONGS_EQUAL(1, ttape_reader_read_group_scale_u16(&gview, 0u, &scale0));
    LONGS_EQUAL(0x0180u, scale0);

    auto ext_full = load_fixture_bytes("accept/extended_with_fp32_aux.ttape");
    LONGS_EQUAL(TTAPE_OK, ttape_v1_validate_tape(ext_full.data(), ext_full.size(), TTAPE_PROFILE_EXTENDED));
}

TEST(TtapeGoldenCorpusGroup, AllSixteenRejectFixturesProduceExactNamedDiagnostics) {
    struct RejectCase {
        const char *rel_path;
        ttape_v1_profile_t profile;
        ttape_v1_error_t expected_err;
    };
    const RejectCase cases[] = {
        {"reject/bad_magic.ttape", TTAPE_PROFILE_CORE, TTAPE_ERR_BAD_MAGIC},
        {"reject/unsupported_version.ttape", TTAPE_PROFILE_CORE, TTAPE_ERR_UNSUPPORTED_VERSION},
        {"reject/bad_section_checksum.ttape", TTAPE_PROFILE_CORE, TTAPE_ERR_CHECKSUM_MISMATCH},
        {"reject/bad_artifact_id.ttape", TTAPE_PROFILE_CORE, TTAPE_ERR_ARTIFACT_ID_MISMATCH},
        {"reject/reserved_ternary_code_0b11.ttape", TTAPE_PROFILE_CORE, TTAPE_ERR_RESERVED_TERNARY_CODE},
        {"reject/nonzero_padding.ttape", TTAPE_PROFILE_CORE, TTAPE_ERR_NONZERO_PADDING},
        {"reject/unknown_flags_bit3.ttape", TTAPE_PROFILE_CORE, TTAPE_ERR_UNKNOWN_FLAGS},
        {"reject/unknown_section_tag.ttape", TTAPE_PROFILE_CORE, TTAPE_ERR_UNKNOWN_TAG},
        {"reject/missing_required_section.ttape", TTAPE_PROFILE_EXTENDED, TTAPE_ERR_MISSING_REQUIRED_SECTION},
        {"reject/malformed_fp32_aux_nan.ttape", TTAPE_PROFILE_EXTENDED, TTAPE_ERR_MALFORMED_FP32_AUX},
        {"reject/core_reader_on_extended.ttape", TTAPE_PROFILE_CORE, TTAPE_ERR_UNSUPPORTED_PROFILE},
        {"reject/core_reader_on_grouped.ttape", TTAPE_PROFILE_CORE, TTAPE_ERR_UNSUPPORTED_PROFILE},
        {"reject/grouped_reader_on_extended.ttape", TTAPE_PROFILE_GROUPED, TTAPE_ERR_UNSUPPORTED_PROFILE},
        {"reject/group_scales_in_core_profile.ttape", TTAPE_PROFILE_GROUPED, TTAPE_ERR_PROFILE_SECTION_FORBIDDEN},
        {"reject/fp32_aux_in_core_profile.ttape", TTAPE_PROFILE_EXTENDED, TTAPE_ERR_PROFILE_SECTION_FORBIDDEN},
        {"reject/invalid_group_size.ttape", TTAPE_PROFILE_GROUPED, TTAPE_ERR_MALFORMED_GROUP_SCALES},
    };
    for (const auto &tc : cases) {
        auto bytes = load_fixture_bytes(tc.rel_path);
        LONGS_EQUAL_TEXT(
            tc.expected_err,
            ttape_v1_validate_tape(bytes.data(), bytes.size(), tc.profile),
            tc.rel_path
        );
    }
}

TEST(TtapeGoldenCorpusGroup, ExhaustivePerByteMutationFlipsAcceptToReject) {
    const char *accept_files[] = {
        "accept/core_minimal_dot4.ttape",
        "accept/core_with_manifest_and_links.ttape",
        "accept/grouped_with_u16_scales.ttape",
        "accept/extended_with_fp32_aux.ttape"
    };
    for (const char *rel : accept_files) {
        auto orig = load_fixture_bytes(rel);
        LONGS_EQUAL(TTAPE_OK, ttape_v1_validate_tape(orig.data(), orig.size(), TTAPE_PROFILE_EXTENDED));
        for (size_t idx = 0u; idx < orig.size(); ++idx) {
            std::vector<uint8_t> mutated = orig;
            mutated[idx] ^= 0x01u;
            ttape_v1_error_t err =
                ttape_v1_validate_tape(mutated.data(), mutated.size(), TTAPE_PROFILE_EXTENDED);
            CHECK_TRUE(err != TTAPE_OK);
        }
    }
}

TEST(TtapeGoldenCorpusGroup, HeaderSectionAndPayloadEdgeBranches) {
    auto base_tape = load_fixture_bytes("accept/core_minimal_dot4.ttape");

    LONGS_EQUAL(TTAPE_ERR_NULL_OR_TRUNCATED, ttape_v1_validate_tape(nullptr, 64u, TTAPE_PROFILE_CORE));
    LONGS_EQUAL(TTAPE_ERR_NULL_OR_TRUNCATED, ttape_v1_validate_tape(base_tape.data(), 16u, TTAPE_PROFILE_CORE));

    // Nonzero header reserved
    {
        auto t = base_tape;
        t[24] = 1u;
        LONGS_EQUAL(TTAPE_ERR_NONZERO_RESERVED, ttape_v1_validate_tape(t.data(), t.size(), TTAPE_PROFILE_CORE));
    }

    // section_count < 3
    {
        auto t = base_tape;
        t[7] = 2u;
        recompute_tape_checksums(t, false, true);
        LONGS_EQUAL(TTAPE_ERR_SECTION_COUNT_MISMATCH, ttape_v1_validate_tape(t.data(), t.size(), TTAPE_PROFILE_CORE));
    }

    // total_length != in_len - 32
    {
        auto t = base_tape;
        t.push_back(0u);
        LONGS_EQUAL(TTAPE_ERR_LENGTH_MISMATCH, ttape_v1_validate_tape(t.data(), t.size(), TTAPE_PROFILE_CORE));
    }

    // Section walk truncation, nonzero reserved, oversized sh.length, trailing byte, tag 0, duplicate tag
    {
        auto t = base_tape;
        t[7] = 4u;
        recompute_tape_checksums(t);
        LONGS_EQUAL(TTAPE_ERR_LENGTH_MISMATCH, ttape_v1_validate_tape(t.data(), t.size(), TTAPE_PROFILE_CORE));
    }
    {
        auto t = base_tape;
        t[32 + 2] = 1u;
        recompute_tape_checksums(t);
        LONGS_EQUAL(TTAPE_ERR_NONZERO_RESERVED, ttape_v1_validate_tape(t.data(), t.size(), TTAPE_PROFILE_CORE));
    }
    {
        auto t = base_tape;
        uint32_t huge_len = 9999u;
        std::memcpy(t.data() + 32 + 4, &huge_len, 4u);
        recompute_tape_checksums(t);
        LONGS_EQUAL(TTAPE_ERR_LENGTH_MISMATCH, ttape_v1_validate_tape(t.data(), t.size(), TTAPE_PROFILE_CORE));
    }
    {
        std::vector<uint8_t> secs(base_tape.begin() + 32, base_tape.end());
        secs.push_back(0xAAu);
        auto t = wrap_tape(secs, 3u, 0x00u);
        LONGS_EQUAL(TTAPE_ERR_LENGTH_MISMATCH, ttape_v1_validate_tape(t.data(), t.size(), TTAPE_PROFILE_CORE));
    }
    {
        auto t = base_tape;
        uint16_t zero_tag = 0u;
        std::memcpy(t.data() + 32, &zero_tag, 2u);
        recompute_tape_checksums(t);
        LONGS_EQUAL(TTAPE_ERR_UNKNOWN_TAG, ttape_v1_validate_tape(t.data(), t.size(), TTAPE_PROFILE_CORE));
    }
    {
        auto t = base_tape;
        uint16_t dup_tag = TTAPE_V1_TAG_WEIGHTS;
        std::memcpy(t.data() + 46, &dup_tag, 2u);
        recompute_tape_checksums(t);
        LONGS_EQUAL(TTAPE_ERR_DUPLICATE_TAG, ttape_v1_validate_tape(t.data(), t.size(), TTAPE_PROFILE_CORE));
    }

    // Missing base sections / manifest / links flag mismatches
    {
        auto t = base_tape;
        uint16_t man_tag = TTAPE_V1_TAG_MANIFEST;
        std::memcpy(t.data() + 60, &man_tag, 2u);
        t[6] = TTAPE_V1_FLAG_MANIFEST;
        recompute_tape_checksums(t);
        LONGS_EQUAL(TTAPE_ERR_MISSING_REQUIRED_SECTION, ttape_v1_validate_tape(t.data(), t.size(), TTAPE_PROFILE_CORE));
    }
    {
        auto t = base_tape;
        uint16_t man_tag = TTAPE_V1_TAG_MANIFEST;
        std::memcpy(t.data() + 46, &man_tag, 2u);
        t[6] = TTAPE_V1_FLAG_MANIFEST;
        recompute_tape_checksums(t);
        LONGS_EQUAL(TTAPE_ERR_MISSING_REQUIRED_SECTION, ttape_v1_validate_tape(t.data(), t.size(), TTAPE_PROFILE_CORE));
    }
    {
        auto t = base_tape;
        uint16_t man_tag = TTAPE_V1_TAG_MANIFEST;
        std::memcpy(t.data() + 32, &man_tag, 2u);
        t[6] = TTAPE_V1_FLAG_MANIFEST;
        recompute_tape_checksums(t);
        LONGS_EQUAL(TTAPE_ERR_MISSING_REQUIRED_SECTION, ttape_v1_validate_tape(t.data(), t.size(), TTAPE_PROFILE_CORE));
    }
    {
        auto t = base_tape;
        t[6] = TTAPE_V1_FLAG_MANIFEST;
        recompute_tape_checksums(t);
        LONGS_EQUAL(TTAPE_ERR_MISSING_REQUIRED_SECTION, ttape_v1_validate_tape(t.data(), t.size(), TTAPE_PROFILE_CORE));
    }
    {
        auto t = base_tape;
        t[6] = TTAPE_V1_FLAG_LINKS;
        recompute_tape_checksums(t);
        LONGS_EQUAL(TTAPE_ERR_MISSING_REQUIRED_SECTION, ttape_v1_validate_tape(t.data(), t.size(), TTAPE_PROFILE_CORE));
    }

    // Arch, scales, manifest, links, group_scales, and fp32_aux validator branches
    auto build3 = [](const std::vector<uint8_t> &w, const std::vector<uint8_t> &sc, const std::vector<uint8_t> &ar, uint8_t flags = 0u) {
        std::vector<uint8_t> secs;
        append_section(secs, TTAPE_V1_TAG_WEIGHTS, w);
        append_section(secs, TTAPE_V1_TAG_SCALES, sc);
        append_section(secs, TTAPE_V1_TAG_ARCH_DESCRIPTOR, ar);
        return wrap_tape(secs, 3u, flags);
    };
    std::vector<uint8_t> good_w = {0x49u, 0x16u};
    std::vector<uint8_t> good_sc = {0xFEu, 0xFFu};
    auto make_arch1 = [](uint16_t kind, uint16_t din, uint16_t dout, uint8_t stages, uint8_t res, uint32_t lat, uint32_t cap, uint16_t n_layers = 1u, uint16_t hdr_res = 0u) {
        std::vector<uint8_t> ar;
        append_u16(ar, n_layers);
        append_u16(ar, hdr_res);
        struct ttape_v1_arch_layer L = {kind, din, dout, stages, res, lat, cap};
        const uint8_t *p = reinterpret_cast<const uint8_t *>(&L);
        ar.insert(ar.end(), p, p + sizeof(L));
        return ar;
    };

    LONGS_EQUAL(TTAPE_ERR_MALFORMED_ARCH, ttape_v1_validate_tape(build3(good_w, good_sc, {1u, 0u}).data(), build3(good_w, good_sc, {1u, 0u}).size(), TTAPE_PROFILE_CORE));
    LONGS_EQUAL(TTAPE_ERR_NONZERO_RESERVED, ttape_v1_validate_tape(build3(good_w, good_sc, make_arch1(4, 4, 2, 2, 0, 2, 16, 1, 1)).data(), build3(good_w, good_sc, make_arch1(4, 4, 2, 2, 0, 2, 16, 1, 1)).size(), TTAPE_PROFILE_CORE));
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_ARCH, ttape_v1_validate_tape(build3(good_w, good_sc, {0u, 0u, 0u, 0u}).data(), build3(good_w, good_sc, {0u, 0u, 0u, 0u}).size(), TTAPE_PROFILE_CORE));
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_ARCH, ttape_v1_validate_tape(build3(good_w, good_sc, make_arch1(4, 4, 2, 2, 0, 2, 16, 2, 0)).data(), build3(good_w, good_sc, make_arch1(4, 4, 2, 2, 0, 2, 16, 2, 0)).size(), TTAPE_PROFILE_CORE));
    LONGS_EQUAL(TTAPE_ERR_NONZERO_RESERVED, ttape_v1_validate_tape(build3(good_w, good_sc, make_arch1(4, 4, 2, 2, 1, 2, 16)).data(), build3(good_w, good_sc, make_arch1(4, 4, 2, 2, 1, 2, 16)).size(), TTAPE_PROFILE_CORE));
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_ARCH, ttape_v1_validate_tape(build3(good_w, good_sc, make_arch1(0, 4, 2, 2, 0, 2, 16)).data(), build3(good_w, good_sc, make_arch1(0, 4, 2, 2, 0, 2, 16)).size(), TTAPE_PROFILE_CORE));
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_ARCH, ttape_v1_validate_tape(build3(good_w, good_sc, make_arch1(8, 4, 2, 2, 0, 2, 16)).data(), build3(good_w, good_sc, make_arch1(8, 4, 2, 2, 0, 2, 16)).size(), TTAPE_PROFILE_CORE));
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_ARCH, ttape_v1_validate_tape(build3(good_w, good_sc, make_arch1(4, 0, 2, 2, 0, 2, 16)).data(), build3(good_w, good_sc, make_arch1(4, 0, 2, 2, 0, 2, 16)).size(), TTAPE_PROFILE_CORE));
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_ARCH, ttape_v1_validate_tape(build3(good_w, good_sc, make_arch1(4, 4, 0, 2, 0, 2, 16)).data(), build3(good_w, good_sc, make_arch1(4, 4, 0, 2, 0, 2, 16)).size(), TTAPE_PROFILE_CORE));
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_ARCH, ttape_v1_validate_tape(build3(good_w, good_sc, make_arch1(4, 4, 2, 2, 0, 2, 0)).data(), build3(good_w, good_sc, make_arch1(4, 4, 2, 2, 0, 2, 0)).size(), TTAPE_PROFILE_CORE));
    LONGS_EQUAL(TTAPE_ERR_PROFILE_SECTION_FORBIDDEN, ttape_v1_validate_tape(build3(good_w, good_sc, make_arch1(6, 4, 2, 0, 0, 2, 16)).data(), build3(good_w, good_sc, make_arch1(6, 4, 2, 0, 0, 2, 16)).size(), TTAPE_PROFILE_CORE));
    // Grouped reader rejects host_embedding layer_kind=6 even when non-core flag + group_scales are present
    {
        auto arch2 = make_arch1(6, 4, 2, 0, 0, 2, 16, 2, 0);
        struct ttape_v1_arch_layer L_dense = {4u, 4u, 2u, 2u, 0u, 2u, 16u};
        const uint8_t *pd = reinterpret_cast<const uint8_t *>(&L_dense);
        arch2.insert(arch2.end(), pd, pd + sizeof(L_dense));
        std::vector<uint8_t> gs = {1u, 0u, 0u, 0u, 1u, 0u, 64u, 0u, 2u, 0u, 1u, 0u, 1u, 0u, 1u, 0u};
        std::vector<uint8_t> secs;
        append_section(secs, TTAPE_V1_TAG_WEIGHTS, good_w);
        append_section(secs, TTAPE_V1_TAG_SCALES, good_sc);
        append_section(secs, TTAPE_V1_TAG_ARCH_DESCRIPTOR, arch2);
        append_section(secs, TTAPE_V1_TAG_GROUP_SCALES_U16, gs);
        auto t = wrap_tape(secs, 4u, TTAPE_V1_FLAG_NON_CORE);
        LONGS_EQUAL(TTAPE_ERR_UNSUPPORTED_PROFILE, ttape_v1_validate_tape(t.data(), t.size(), TTAPE_PROFILE_GROUPED));
    }
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_ARCH, ttape_v1_validate_tape(build3(good_w, good_sc, make_arch1(4, 4, 2, 3, 0, 3, 16)).data(), build3(good_w, good_sc, make_arch1(4, 4, 2, 3, 0, 3, 16)).size(), TTAPE_PROFILE_CORE));
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_ARCH, ttape_v1_validate_tape(build3(good_w, good_sc, make_arch1(4, 4, 2, 2, 0, 1, 16)).data(), build3(good_w, good_sc, make_arch1(4, 4, 2, 2, 0, 1, 16)).size(), TTAPE_PROFILE_CORE));
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_ARCH, ttape_v1_validate_tape(build3(good_w, good_sc, make_arch1(1, 4, 4, 1, 0, 1, 16)).data(), build3(good_w, good_sc, make_arch1(1, 4, 4, 1, 0, 1, 16)).size(), TTAPE_PROFILE_CORE));
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_ARCH, ttape_v1_validate_tape(build3(good_w, good_sc, make_arch1(1, 4, 2, 0, 0, 1, 16)).data(), build3(good_w, good_sc, make_arch1(1, 4, 2, 0, 0, 1, 16)).size(), TTAPE_PROFILE_CORE));
    LONGS_EQUAL(TTAPE_ERR_DIM_PAYLOAD_MISMATCH, ttape_v1_validate_tape(build3(good_w, good_sc, make_arch1(1, 4, 4, 0, 0, 1, 16)).data(), build3(good_w, good_sc, make_arch1(1, 4, 4, 0, 0, 1, 16)).size(), TTAPE_PROFILE_CORE));
    LONGS_EQUAL(TTAPE_ERR_DIM_PAYLOAD_MISMATCH, ttape_v1_validate_tape(build3({0x49u}, good_sc, make_arch1(4, 4, 2, 2, 0, 2, 16)).data(), build3({0x49u}, good_sc, make_arch1(4, 4, 2, 2, 0, 2, 16)).size(), TTAPE_PROFILE_CORE));
    LONGS_EQUAL(TTAPE_ERR_DIM_PAYLOAD_MISMATCH, ttape_v1_validate_tape(build3(good_w, {0xFEu, 0xFFu, 0x00u, 0x00u}, make_arch1(4, 4, 2, 2, 0, 2, 16)).data(), build3(good_w, {0xFEu, 0xFFu, 0x00u, 0x00u}, make_arch1(4, 4, 2, 2, 0, 2, 16)).size(), TTAPE_PROFILE_CORE));
    LONGS_EQUAL(TTAPE_ERR_INVALID_SCALE, ttape_v1_validate_tape(build3(good_w, {0xE1u, 0xFFu}, make_arch1(4, 4, 2, 2, 0, 2, 16)).data(), build3(good_w, {0xE1u, 0xFFu}, make_arch1(4, 4, 2, 2, 0, 2, 16)).size(), TTAPE_PROFILE_CORE));
    LONGS_EQUAL(TTAPE_ERR_INVALID_SCALE, ttape_v1_validate_tape(build3(good_w, {31u, 0u}, make_arch1(4, 4, 2, 2, 0, 2, 16)).data(), build3(good_w, {31u, 0u}, make_arch1(4, 4, 2, 2, 0, 2, 16)).size(), TTAPE_PROFILE_CORE));

    // Manifest, Link, GroupScales, and FP32Aux direct helper edge branches
    uint8_t short_man[1] = {0u};
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_MANIFEST, ttape_v1_validate_manifest(short_man, 1u));
    uint8_t zero_flen[2] = {0u, 0u};
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_MANIFEST, ttape_v1_validate_manifest(zero_flen, 2u));
    uint8_t big_flen[2] = {10u, 0u};
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_MANIFEST, ttape_v1_validate_manifest(big_flen, 2u));
    std::vector<uint8_t> bad_run_id = {1u, 0u, 'a', 4u, 0u, 'r', 'u', 'n', '1'};
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_MANIFEST, ttape_v1_validate_manifest(bad_run_id.data(), static_cast<uint32_t>(bad_run_id.size())));
    std::vector<uint8_t> m_short_hash;
    append_u16(m_short_hash, 1u); m_short_hash.push_back('p');
    append_u16(m_short_hash, 32u); m_short_hash.insert(m_short_hash.end(), 32u, 'r');
    append_u16(m_short_hash, 1u); m_short_hash.push_back('q');
    append_u16(m_short_hash, 1u); m_short_hash.push_back('c');
    append_u16(m_short_hash, 4u); m_short_hash.insert(m_short_hash.end(), 4u, 'h');
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_MANIFEST, ttape_v1_validate_manifest(m_short_hash.data(), static_cast<uint32_t>(m_short_hash.size())));
    std::vector<uint8_t> m_trailing = m_short_hash;
    m_trailing[m_trailing.size() - 6u] = 8u;
    m_trailing.insert(m_trailing.end(), 4u, 'h');
    m_trailing.push_back('X');
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_MANIFEST, ttape_v1_validate_manifest(m_trailing.data(), static_cast<uint32_t>(m_trailing.size())));

    uint8_t dummy = 0u;
    LONGS_EQUAL(TTAPE_ERR_INVALID_LINK, ttape_v1_validate_links(&dummy, 0u));
    LONGS_EQUAL(TTAPE_ERR_INVALID_LINK, ttape_v1_validate_links(&dummy, 2u));
    int32_t d_zero = 0;
    LONGS_EQUAL(TTAPE_ERR_INVALID_LINK, ttape_v1_validate_links(reinterpret_cast<const uint8_t *>(&d_zero), 4u));
    int32_t d_unaligned = 2;
    LONGS_EQUAL(TTAPE_ERR_INVALID_LINK, ttape_v1_validate_links(reinterpret_cast<const uint8_t *>(&d_unaligned), 8u));
    int32_t d_neg = -4;
    LONGS_EQUAL(TTAPE_ERR_INVALID_LINK, ttape_v1_validate_links(reinterpret_cast<const uint8_t *>(&d_neg), 4u));
    int32_t d_oob = 8;
    LONGS_EQUAL(TTAPE_ERR_INVALID_LINK, ttape_v1_validate_links(reinterpret_cast<const uint8_t *>(&d_oob), 4u));

    // Group scales edge branches
    auto arch1_dense = make_arch1(4, 4, 2, 2, 0, 2, 16);
    uint8_t short_gs[2] = {0u, 0u};
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_GROUP_SCALES, ttape_v1_validate_group_scales(short_gs, 2u, arch1_dense.data(), 1u, good_sc.data()));
    uint8_t res_gs[4] = {1u, 0u, 1u, 0u};
    LONGS_EQUAL(TTAPE_ERR_NONZERO_RESERVED, ttape_v1_validate_group_scales(res_gs, 4u, arch1_dense.data(), 1u, good_sc.data()));
    uint8_t bad_cnt_gs[4] = {2u, 0u, 0u, 0u};
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_GROUP_SCALES, ttape_v1_validate_group_scales(bad_cnt_gs, 4u, arch1_dense.data(), 1u, good_sc.data()));
    uint8_t trunc_gs[6] = {1u, 0u, 0u, 0u, 0u, 0u};
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_GROUP_SCALES, ttape_v1_validate_group_scales(trunc_gs, 6u, arch1_dense.data(), 1u, good_sc.data()));
    uint8_t rec_res_gs[12] = {1u, 0u, 0u, 0u, 0u, 0u, 64u, 0u, 2u, 1u, 1u, 0u};
    LONGS_EQUAL(TTAPE_ERR_NONZERO_RESERVED, ttape_v1_validate_group_scales(rec_res_gs, 12u, arch1_dense.data(), 1u, good_sc.data()));
    uint8_t bad_lidx_gs[12] = {1u, 0u, 0u, 0u, 1u, 0u, 64u, 0u, 2u, 0u, 1u, 0u};
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_GROUP_SCALES, ttape_v1_validate_group_scales(bad_lidx_gs, 12u, arch1_dense.data(), 1u, good_sc.data()));
    uint8_t bad_shift_gs[12] = {1u, 0u, 0u, 0u, 0u, 0u, 64u, 0u, 5u, 0u, 1u, 0u}; // shift=5 != -(-2)=2
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_GROUP_SCALES, ttape_v1_validate_group_scales(bad_shift_gs, 12u, arch1_dense.data(), 1u, good_sc.data()));
    uint8_t bad_ng_gs[12] = {1u, 0u, 0u, 0u, 0u, 0u, 64u, 0u, 2u, 0u, 2u, 0u};
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_GROUP_SCALES, ttape_v1_validate_group_scales(bad_ng_gs, 12u, arch1_dense.data(), 1u, good_sc.data()));
    uint8_t trunc_pay_gs[14] = {1u, 0u, 0u, 0u, 0u, 0u, 64u, 0u, 2u, 0u, 1u, 0u, 1u, 0u}; // dim_out=2 needs 4 bytes
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_GROUP_SCALES, ttape_v1_validate_group_scales(trunc_pay_gs, 14u, arch1_dense.data(), 1u, good_sc.data()));
    uint8_t trail_gs[17] = {1u, 0u, 0u, 0u, 0u, 0u, 128u, 0u, 2u, 0u, 1u, 0u, 1u, 0u, 1u, 0u, 9u};
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_GROUP_SCALES, ttape_v1_validate_group_scales(trail_gs, 17u, arch1_dense.data(), 1u, good_sc.data()));
    uint8_t valid_256_gs[16] = {1u, 0u, 0u, 0u, 0u, 0u, 0u, 1u, 2u, 0u, 1u, 0u, 1u, 0u, 1u, 0u}; // G=256
    LONGS_EQUAL(TTAPE_OK, ttape_v1_validate_group_scales(valid_256_gs, 16u, arch1_dense.data(), 1u, good_sc.data()));

    // FP32 aux edge branches
    auto arch2 = make_arch1(4, 4, 2, 2, 0, 2, 16, 2, 0);
    struct ttape_v1_arch_layer L_soft = {7u, 2u, 2u, 0u, 0u, 1u, 16u};
    const uint8_t *ps = reinterpret_cast<const uint8_t *>(&L_soft);
    arch2.insert(arch2.end(), ps, ps + sizeof(L_soft));
    uint8_t short_aux[2] = {0u, 0u};
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_FP32_AUX, ttape_v1_validate_fp32_aux(short_aux, 2u, arch2.data(), 2u));
    uint8_t res_aux[4] = {1u, 0u, 1u, 0u};
    LONGS_EQUAL(TTAPE_ERR_NONZERO_RESERVED, ttape_v1_validate_fp32_aux(res_aux, 4u, arch2.data(), 2u));
    uint8_t zero_cnt[4] = {0u, 0u, 0u, 0u};
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_FP32_AUX, ttape_v1_validate_fp32_aux(zero_cnt, 4u, arch2.data(), 2u));
    uint8_t trunc_rec[6] = {1u, 0u, 0u, 0u, 0u, 0u};
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_FP32_AUX, ttape_v1_validate_fp32_aux(trunc_rec, 6u, arch2.data(), 2u));
    std::vector<uint8_t> rec_res = {1u, 0u, 0u, 0u, 1u, 0u, 3u, 1u, 2u, 0u, 0u, 0u};
    LONGS_EQUAL(TTAPE_ERR_NONZERO_RESERVED, ttape_v1_validate_fp32_aux(rec_res.data(), static_cast<uint32_t>(rec_res.size()), arch2.data(), 2u));
    std::vector<uint8_t> bad_lidx = {1u, 0u, 0u, 0u, 5u, 0u, 3u, 0u, 2u, 0u, 0u, 0u};
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_FP32_AUX, ttape_v1_validate_fp32_aux(bad_lidx.data(), static_cast<uint32_t>(bad_lidx.size()), arch2.data(), 2u));
    std::vector<uint8_t> bad_role0 = {1u, 0u, 0u, 0u, 1u, 0u, 0u, 0u, 2u, 0u, 0u, 0u};
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_FP32_AUX, ttape_v1_validate_fp32_aux(bad_role0.data(), static_cast<uint32_t>(bad_role0.size()), arch2.data(), 2u));
    std::vector<uint8_t> bad_role4 = {1u, 0u, 0u, 0u, 1u, 0u, 4u, 0u, 2u, 0u, 0u, 0u};
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_FP32_AUX, ttape_v1_validate_fp32_aux(bad_role4.data(), static_cast<uint32_t>(bad_role4.size()), arch2.data(), 2u));
    std::vector<uint8_t> zero_elems = {1u, 0u, 0u, 0u, 1u, 0u, 3u, 0u, 0u, 0u, 0u, 0u};
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_FP32_AUX, ttape_v1_validate_fp32_aux(zero_elems.data(), static_cast<uint32_t>(zero_elems.size()), arch2.data(), 2u));
    std::vector<uint8_t> big_elems = {1u, 0u, 0u, 0u, 1u, 0u, 3u, 0u, 99u, 0u, 0u, 0u};
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_FP32_AUX, ttape_v1_validate_fp32_aux(big_elems.data(), static_cast<uint32_t>(big_elems.size()), arch2.data(), 2u));
    std::vector<uint8_t> wrong_kind = {1u, 0u, 0u, 0u, 0u, 0u, 3u, 0u, 2u, 0u, 0u, 0u};
    wrong_kind.insert(wrong_kind.end(), 8u, 0u);
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_FP32_AUX, ttape_v1_validate_fp32_aux(wrong_kind.data(), static_cast<uint32_t>(wrong_kind.size()), arch2.data(), 2u));
    std::vector<uint8_t> wrong_elems = {1u, 0u, 0u, 0u, 1u, 0u, 3u, 0u, 1u, 0u, 0u, 0u};
    wrong_elems.insert(wrong_elems.end(), 4u, 0u);
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_FP32_AUX, ttape_v1_validate_fp32_aux(wrong_elems.data(), static_cast<uint32_t>(wrong_elems.size()), arch2.data(), 2u));
    std::vector<uint8_t> valid_role3 = {1u, 0u, 0u, 0u, 1u, 0u, 3u, 0u, 2u, 0u, 0u, 0u};
    valid_role3.insert(valid_role3.end(), 8u, 0u);
    LONGS_EQUAL(TTAPE_OK, ttape_v1_validate_fp32_aux(valid_role3.data(), static_cast<uint32_t>(valid_role3.size()), arch2.data(), 2u));
    valid_role3.push_back(0u);
    LONGS_EQUAL(TTAPE_ERR_MALFORMED_FP32_AUX, ttape_v1_validate_fp32_aux(valid_role3.data(), static_cast<uint32_t>(valid_role3.size()), arch2.data(), 2u));

    // Group scale reader defensive bounds
    struct ttape_reader core_r;
    LONGS_EQUAL(TTAPE_OK, ttape_reader_open(&core_r, base_tape.data(), base_tape.size(), TTAPE_PROFILE_CORE));
    struct ttape_group_scales_view gview;
    LONGS_EQUAL(0, ttape_reader_get_group_scales(nullptr, 0u, &gview));
    LONGS_EQUAL(0, ttape_reader_get_group_scales(&core_r, 0u, nullptr));
    LONGS_EQUAL(0, ttape_reader_get_group_scales(&core_r, 0u, &gview));
    uint16_t s_out = 0u;
    LONGS_EQUAL(0, ttape_reader_read_group_scale_u16(nullptr, 0u, &s_out));
    auto grp_u16 = load_fixture_bytes("accept/grouped_with_u16_scales.ttape");
    struct ttape_reader grp_r;
    LONGS_EQUAL(TTAPE_OK, ttape_reader_open(&grp_r, grp_u16.data(), grp_u16.size(), TTAPE_PROFILE_GROUPED));
    LONGS_EQUAL(0, ttape_reader_get_group_scales(&grp_r, 5u, &gview));
    LONGS_EQUAL(1, ttape_reader_get_group_scales(&grp_r, 0u, &gview));
    LONGS_EQUAL(0, ttape_reader_read_group_scale_u16(&gview, 0u, nullptr));
    LONGS_EQUAL(0, ttape_reader_read_group_scale_u16(&gview, 5u, &s_out));
}
