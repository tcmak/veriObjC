#include <CppUTest/TestHarness.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "vstd/ttape_reader.h"
#include "host/scheduler.h"

namespace {

std::vector<uint8_t> read_fixture(const char *rel_path) {
    std::string full =
        std::string(VERIOBJC_SOURCE_DIR) + "/third_party/TernaryTape/fixtures/" + rel_path;
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

} // namespace

TEST_GROUP(VeriStreamRuntimeSliceGroup) {};

TEST(VeriStreamRuntimeSliceGroup, ParseCoreAndGroupedTapesAndRunPureCpuFallback) {
    std::vector<uint8_t> core_tape = read_fixture("accept/core_minimal_dot4.ttape");

    struct ttape_reader reader;
    ttape_v1_error_t st = ttape_reader_open(
        &reader, core_tape.data(), core_tape.size(), TTAPE_PROFILE_CORE);
    LONGS_EQUAL(TTAPE_OK, st);
    LONGS_EQUAL(TTAPE_PROFILE_CORE, reader.profile);
    LONGS_EQUAL(1u, reader.layer_count);
    LONGS_EQUAL(0u, reader.fp32_aux_count);

    struct ttape_layer_desc layer0;
    LONGS_EQUAL(1, ttape_reader_get_layer(&reader, 0, &layer0));
    LONGS_EQUAL(TTAPE_LAYER_TERNARY_DENSE, layer0.layer_kind);
    LONGS_EQUAL(4u, layer0.dim_in);
    LONGS_EQUAL(2u, layer0.dim_out);
    LONGS_EQUAL(2u, layer0.reduce_stages);

    int16_t scale_exp = 0;
    LONGS_EQUAL(1, ttape_reader_get_scale_exp(&reader, layer0.scale_index, &scale_exp));
    LONGS_EQUAL(-2, scale_exp);

    const int32_t act_in[4] = {12, 20, 28, 36};
    int32_t cpu_acc[2] = {0, 0};
    int32_t cpu_scaled[2] = {0, 0};
    LONGS_EQUAL(
        TTAPE_SCHED_OK,
        ttape_scheduler_run_layer_cpu_fallback(&reader, 0, act_in, cpu_acc, cpu_scaled));
    LONGS_EQUAL(28, cpu_acc[0]);
    LONGS_EQUAL(36, cpu_acc[1]);
    LONGS_EQUAL(7, cpu_scaled[0]);
    LONGS_EQUAL(9, cpu_scaled[1]);

    // Grouped tape (0x0006 group_scales_u16: N=1, K=6, G=64, shift=8, scale_u16=384)
    std::vector<uint8_t> grp_tape = read_fixture("accept/grouped_with_u16_scales.ttape");
    struct ttape_reader grp_reader;
    LONGS_EQUAL(
        TTAPE_OK,
        ttape_reader_open(&grp_reader, grp_tape.data(), grp_tape.size(), TTAPE_PROFILE_GROUPED));
    const int32_t act6[6] = {-128, 50, 99, 30, -20, 10};
    int32_t grp_acc[1] = {0};
    int32_t grp_scaled[1] = {0};
    LONGS_EQUAL(
        TTAPE_SCHED_OK,
        ttape_scheduler_run_layer_cpu_fallback(&grp_reader, 0, act6, grp_acc, grp_scaled));
    LONGS_EQUAL(-138, grp_acc[0]);
    // Round-half-up: (-138 * 384 + 128) >> 8 = -52864 >> 8 = -207
    LONGS_EQUAL(-207, grp_scaled[0]);
}

TEST(VeriStreamRuntimeSliceGroup, ExtendedTapeScheduleAndHostOnlyFp32AuxEnforcement) {
    std::vector<uint8_t> ext_tape = read_fixture("accept/extended_with_fp32_aux.ttape");
    std::vector<uint8_t> core_manifest_tape =
        read_fixture("accept/core_with_manifest_and_links.ttape");

    struct ttape_reader core_m_reader;
    LONGS_EQUAL(
        TTAPE_OK,
        ttape_reader_open(
            &core_m_reader,
            core_manifest_tape.data(),
            core_manifest_tape.size(),
            TTAPE_PROFILE_CORE));
    CHECK(core_m_reader.manifest.data != nullptr);
    CHECK(core_m_reader.link_table.data != nullptr);

    struct ttape_reader reader;
    LONGS_EQUAL(
        TTAPE_OK,
        ttape_reader_open(&reader, ext_tape.data(), ext_tape.size(), TTAPE_PROFILE_EXTENDED));
    LONGS_EQUAL(TTAPE_PROFILE_EXTENDED, reader.profile);
    LONGS_EQUAL(3u, reader.layer_count);
    LONGS_EQUAL(2u, reader.fp32_aux_count);

    struct ttape_schedule_plan plan;
    LONGS_EQUAL(1, ttape_scheduler_build_plan(&reader, 4, &plan));
    LONGS_EQUAL(3u, plan.count);

    LONGS_EQUAL(TTAPE_SCHED_OP_HOST_FP32_AUX, plan.steps[0].op_kind);
    LONGS_EQUAL(0u, plan.steps[0].is_hardware_visible);
    LONGS_EQUAL(TTAPE_SCHED_OP_HOST_FP32_AUX, plan.steps[1].op_kind);
    LONGS_EQUAL(0u, plan.steps[1].is_hardware_visible);
    LONGS_EQUAL(TTAPE_SCHED_OP_TERNARY_MATMUL, plan.steps[2].op_kind);
    LONGS_EQUAL(1u, plan.steps[2].is_hardware_visible);
    LONGS_EQUAL(2u, plan.steps[2].num_tiles);

    const float host_in8[8] = {2.0f, 4.0f, 2.0f, 4.0f, 2.0f, 4.0f, 2.0f, 8.0f};
    float emb_out[8] = {0.0f};
    LONGS_EQUAL(1, ttape_scheduler_run_host_fp32_aux(&reader, 0, host_in8, 8, emb_out));
    DOUBLES_EQUAL(2.0f * 0.25f, emb_out[0], 1e-6);
    DOUBLES_EQUAL(4.0f * -0.5f, emb_out[1], 1e-6);

    const float rms_in4[4] = {1.5f, -2.5f, 3.5f, 4.5f};
    float rms_out[4] = {0.0f};
    LONGS_EQUAL(1, ttape_scheduler_run_host_fp32_aux(&reader, 1, rms_in4, 4, rms_out));
    DOUBLES_EQUAL(1.5f, rms_out[0], 1e-6);
    DOUBLES_EQUAL(-2.5f, rms_out[1], 1e-6);

    const int32_t act4[4] = {12, 20, 28, 36};
    int32_t acc2[2] = {0, 0};
    int32_t scaled2[2] = {0, 0};
    LONGS_EQUAL(
        TTAPE_SCHED_OK,
        ttape_scheduler_run_layer_cpu_fallback(&reader, 2, act4, acc2, scaled2));
    LONGS_EQUAL(28, acc2[0]);
    LONGS_EQUAL(36, acc2[1]);
    LONGS_EQUAL(7, scaled2[0]);
    LONGS_EQUAL(9, scaled2[1]);

    // Host-only fp32_aux bytes MUST be rejected from ternary weight buffer descriptors
    struct ttape_fp32_aux_tensor_view aux0;
    LONGS_EQUAL(1, ttape_reader_get_fp32_aux(&reader, 0, &aux0));
    float elem0 = 0.0f;
    LONGS_EQUAL(1, ttape_reader_read_fp32_elem(&aux0, 0, &elem0));
    DOUBLES_EQUAL(0.25f, elem0, 1e-6);

    LONGS_EQUAL(1, ttape_reader_is_host_only_ptr(&reader, aux0.raw_le_f32, 4));
    LONGS_EQUAL(0, ttape_reader_is_host_only_ptr(&reader, reader.weights.data, 1));

    struct ttape_cpu_buffer_desc forbidden_desc;
    std::memset(&forbidden_desc, 0, sizeof(forbidden_desc));
    forbidden_desc.weight_bytes = aux0.raw_le_f32;
    forbidden_desc.weight_len = 4;
    forbidden_desc.act_in = act4;
    forbidden_desc.rows = 1;
    forbidden_desc.cols = 4;
    forbidden_desc.scale_exp = 0;
    forbidden_desc.acc_out = acc2;
    forbidden_desc.scaled_out = scaled2;
    LONGS_EQUAL(
        TTAPE_SCHED_ERR_HOST_ONLY_AUX_FORBIDDEN,
        ttape_cpu_run_ternary_matmul(&reader, &forbidden_desc));
}

TEST(VeriStreamRuntimeSliceGroup, ReaderAndSchedulerDefensiveEdgeBranches) {
    std::vector<uint8_t> core_tape = read_fixture("accept/core_minimal_dot4.ttape");
    std::vector<uint8_t> bad_magic = read_fixture("reject/bad_magic.ttape");

    struct ttape_reader reader;
    LONGS_EQUAL(
        TTAPE_ERR_NULL_OR_TRUNCATED,
        ttape_reader_open(nullptr, core_tape.data(), core_tape.size(), TTAPE_PROFILE_CORE));
    LONGS_EQUAL(
        TTAPE_ERR_BAD_MAGIC,
        ttape_reader_open(&reader, bad_magic.data(), bad_magic.size(), TTAPE_PROFILE_CORE));
    LONGS_EQUAL(
        TTAPE_OK,
        ttape_reader_open(&reader, core_tape.data(), core_tape.size(), TTAPE_PROFILE_CORE));

    struct ttape_layer_desc ldesc;
    LONGS_EQUAL(0, ttape_reader_get_layer(nullptr, 0, &ldesc));
    LONGS_EQUAL(0, ttape_reader_get_layer(&reader, 0, nullptr));
    LONGS_EQUAL(0, ttape_reader_get_layer(&reader, 99, &ldesc));

    int16_t sc = 0;
    LONGS_EQUAL(0, ttape_reader_get_scale_exp(nullptr, 0, &sc));
    LONGS_EQUAL(0, ttape_reader_get_scale_exp(&reader, 0, nullptr));
    LONGS_EQUAL(0, ttape_reader_get_scale_exp(&reader, 99, &sc));

    struct ttape_fp32_aux_tensor_view aux_view;
    LONGS_EQUAL(0, ttape_reader_get_fp32_aux(nullptr, 0, &aux_view));
    LONGS_EQUAL(0, ttape_reader_get_fp32_aux(&reader, 0, nullptr));
    LONGS_EQUAL(0, ttape_reader_get_fp32_aux(&reader, 0, &aux_view));

    std::vector<uint8_t> ext_tape = read_fixture("accept/extended_with_fp32_aux.ttape");
    struct ttape_reader ext_reader;
    LONGS_EQUAL(
        TTAPE_OK,
        ttape_reader_open(&ext_reader, ext_tape.data(), ext_tape.size(), TTAPE_PROFILE_EXTENDED));
    LONGS_EQUAL(0, ttape_reader_get_fp32_aux(&ext_reader, 99, &aux_view));
    LONGS_EQUAL(1, ttape_reader_get_fp32_aux(&ext_reader, 1, &aux_view));

    float fval = 0.0f;
    LONGS_EQUAL(0, ttape_reader_read_fp32_elem(nullptr, 0, &fval));
    LONGS_EQUAL(0, ttape_reader_read_fp32_elem(&aux_view, 0, nullptr));
    LONGS_EQUAL(0, ttape_reader_read_fp32_elem(&aux_view, 99, &fval));

    LONGS_EQUAL(0, ttape_reader_is_host_only_ptr(nullptr, core_tape.data(), 4));
    LONGS_EQUAL(0, ttape_reader_is_host_only_ptr(&reader, core_tape.data(), 4));
    LONGS_EQUAL(0, ttape_reader_is_host_only_ptr(&ext_reader, nullptr, 4));
    LONGS_EQUAL(0, ttape_reader_is_host_only_ptr(&ext_reader, ext_tape.data(), 4));
    LONGS_EQUAL(
        0,
        ttape_reader_is_host_only_ptr(
            &ext_reader,
            ext_reader.fp32_aux.data + ext_reader.fp32_aux.length,
            4));

    const int32_t act4[4] = {1, 2, 3, 4};
    int32_t acc2[2] = {0, 0};
    int32_t sc2[2] = {0, 0};
    struct ttape_cpu_buffer_desc desc;
    std::memset(&desc, 0, sizeof(desc));
    desc.weight_bytes = reader.weights.data;
    desc.weight_len = reader.weights.length;
    desc.act_in = act4;
    desc.rows = 2;
    desc.cols = 4;
    desc.scale_exp = 1;
    desc.acc_out = acc2;
    desc.scaled_out = sc2;
    LONGS_EQUAL(TTAPE_SCHED_OK, ttape_cpu_run_ternary_matmul(nullptr, &desc));
    LONGS_EQUAL(TTAPE_SCHED_ERR_INVALID_ARG, ttape_cpu_run_ternary_matmul(&reader, nullptr));

    struct ttape_cpu_buffer_desc bad_desc = desc;
    bad_desc.weight_bytes = nullptr;
    LONGS_EQUAL(TTAPE_SCHED_ERR_INVALID_ARG, ttape_cpu_run_ternary_matmul(&reader, &bad_desc));
    bad_desc = desc;
    bad_desc.act_in = nullptr;
    LONGS_EQUAL(TTAPE_SCHED_ERR_INVALID_ARG, ttape_cpu_run_ternary_matmul(&reader, &bad_desc));
    bad_desc = desc;
    bad_desc.acc_out = nullptr;
    LONGS_EQUAL(TTAPE_SCHED_ERR_INVALID_ARG, ttape_cpu_run_ternary_matmul(&reader, &bad_desc));
    bad_desc = desc;
    bad_desc.scaled_out = nullptr;
    LONGS_EQUAL(TTAPE_SCHED_ERR_INVALID_ARG, ttape_cpu_run_ternary_matmul(&reader, &bad_desc));

    bad_desc = desc;
    bad_desc.rows = 0;
    LONGS_EQUAL(TTAPE_SCHED_ERR_DIM_MISMATCH, ttape_cpu_run_ternary_matmul(&reader, &bad_desc));
    bad_desc = desc;
    bad_desc.cols = 0;
    LONGS_EQUAL(TTAPE_SCHED_ERR_DIM_MISMATCH, ttape_cpu_run_ternary_matmul(&reader, &bad_desc));
    bad_desc = desc;
    bad_desc.weight_len = 0;
    LONGS_EQUAL(TTAPE_SCHED_ERR_DIM_MISMATCH, ttape_cpu_run_ternary_matmul(&reader, &bad_desc));

    const uint8_t bad_trit_weight[2] = {0x03u, 0x00u};
    bad_desc = desc;
    bad_desc.weight_bytes = bad_trit_weight;
    bad_desc.weight_len = 2;
    LONGS_EQUAL(TTAPE_SCHED_ERR_RESERVED_TRIT, ttape_cpu_run_ternary_matmul(&reader, &bad_desc));

    struct ttape_schedule_plan plan;
    LONGS_EQUAL(0, ttape_scheduler_build_plan(nullptr, 4, &plan));
    LONGS_EQUAL(0, ttape_scheduler_build_plan(&reader, 4, nullptr));
    LONGS_EQUAL(0, ttape_scheduler_build_plan(&reader, 0, &plan));
    LONGS_EQUAL(0, ttape_scheduler_build_plan(&reader, 3, &plan));

    struct ttape_reader huge_layer_reader = reader;
    huge_layer_reader.layer_count = 17;
    LONGS_EQUAL(0, ttape_scheduler_build_plan(&huge_layer_reader, 4, &plan));

    LONGS_EQUAL(
        TTAPE_SCHED_ERR_INVALID_ARG,
        ttape_scheduler_run_layer_cpu_fallback(nullptr, 0, act4, acc2, sc2));
    LONGS_EQUAL(
        TTAPE_SCHED_ERR_INVALID_ARG,
        ttape_scheduler_run_layer_cpu_fallback(&reader, 0, nullptr, acc2, sc2));
    LONGS_EQUAL(
        TTAPE_SCHED_ERR_INVALID_ARG,
        ttape_scheduler_run_layer_cpu_fallback(&reader, 0, act4, nullptr, sc2));
    LONGS_EQUAL(
        TTAPE_SCHED_ERR_INVALID_ARG,
        ttape_scheduler_run_layer_cpu_fallback(&reader, 0, act4, acc2, nullptr));
    LONGS_EQUAL(
        TTAPE_SCHED_ERR_INVALID_ARG,
        ttape_scheduler_run_layer_cpu_fallback(&reader, 99, act4, acc2, sc2));
    LONGS_EQUAL(
        TTAPE_SCHED_ERR_HOST_ONLY_AUX_FORBIDDEN,
        ttape_scheduler_run_layer_cpu_fallback(&ext_reader, 0, act4, acc2, sc2));

    float hin[8] = {1.0f};
    float hout[8] = {0.0f};
    LONGS_EQUAL(0, ttape_scheduler_run_host_fp32_aux(nullptr, 0, hin, 8, hout));
    LONGS_EQUAL(0, ttape_scheduler_run_host_fp32_aux(&ext_reader, 0, nullptr, 8, hout));
    LONGS_EQUAL(0, ttape_scheduler_run_host_fp32_aux(&ext_reader, 0, hin, 8, nullptr));
    LONGS_EQUAL(0, ttape_scheduler_run_host_fp32_aux(&ext_reader, 2, hin, 8, hout));
    LONGS_EQUAL(0, ttape_scheduler_run_host_fp32_aux(&ext_reader, 0, hin, 3, hout));
}
