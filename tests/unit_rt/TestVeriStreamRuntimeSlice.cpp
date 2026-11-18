#include <CppUTest/TestHarness.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "rt/vtpe_reader.h"
#include "rt/dispatch.h"
#include "rt/scheduler.h"

namespace {

std::vector<uint8_t> read_fixture(const char *rel_path) {
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

} // namespace

TEST_GROUP(VeriStreamRuntimeSliceGroup) {};

// Task 5.7 Acceptance: "parse one Core-profile tape, run one matmul on the
// setun accelerator, return C, bit-exact vs golden"
TEST(VeriStreamRuntimeSliceGroup, ParseCoreTapeAndRunMatmulOnSetunAndHlsBitExact) {
    std::vector<uint8_t> core_tape = read_fixture("fixtures/vtpe_v1/accept/core_minimal_dot4.bin");

    struct vtpe_reader reader;
    vtpe_v1_error_t st = vtpe_reader_open(
        &reader, core_tape.data(), core_tape.size(), VTPE_PROFILE_CORE);
    LONGS_EQUAL(VTPE_OK, st);
    LONGS_EQUAL(0u, reader.profile);
    LONGS_EQUAL(1u, reader.layer_count);
    LONGS_EQUAL(0u, reader.fp32_aux_count);

    struct vtpe_layer_desc layer0;
    LONGS_EQUAL(1, vtpe_reader_get_layer(&reader, 0, &layer0));
    LONGS_EQUAL(VTPE_LAYER_TERNARY_DENSE, layer0.layer_kind);
    LONGS_EQUAL(4u, layer0.dim_in);
    LONGS_EQUAL(2u, layer0.dim_out);
    LONGS_EQUAL(2u, layer0.reduce_stages);

    int16_t scale_exp = 0;
    LONGS_EQUAL(1, vtpe_reader_get_scale_exp(&reader, layer0.scale_index, &scale_exp));
    LONGS_EQUAL(-2, scale_exp); // 2^-2

    // core_minimal_dot4.bin has 2x4 ternary weights:
    //   Row 0: [ 1, -1,  0,  1] dot [12, 20, 28, 36] = 12 - 20 + 0 + 36 = 28; scaled (>>2) = 7
    //   Row 1: [-1,  1,  1,  0] dot [12, 20, 28, 36] = -12 + 20 + 28 + 0 = 36; scaled (>>2) = 9
    const int32_t act_in[4] = {12, 20, 28, 36};
    int32_t setun_acc[2] = {0, 0};
    int32_t setun_scaled[2] = {0, 0};

    struct vtpe_dispatch_ctx setun_ctx;
    LONGS_EQUAL(1, vtpe_dispatch_init(&setun_ctx, VTPE_BACKEND_SETUN_LITEX, &reader));
    LONGS_EQUAL(
        VTPE_DISPATCH_OK,
        vtpe_scheduler_run_layer_matmul(&reader, &setun_ctx, 0, act_in, setun_acc, setun_scaled));
    LONGS_EQUAL(28, setun_acc[0]);
    LONGS_EQUAL(36, setun_acc[1]);
    LONGS_EQUAL(7, setun_scaled[0]);
    LONGS_EQUAL(9, setun_scaled[1]);
    LONGS_EQUAL(1u, setun_ctx.setun_regs.irq_count);
    LONGS_EQUAL(2u, setun_ctx.setun_regs.dma_rows);
    LONGS_EQUAL(4u, setun_ctx.setun_regs.dma_cols);
    CHECK(setun_ctx.setun_regs.cycles_elapsed > 0u);

    // Run identical layer matmul on the kit's HLS tile backend and verify bit-exact equality
    int32_t hls_acc[2] = {0, 0};
    int32_t hls_scaled[2] = {0, 0};
    struct vtpe_dispatch_ctx hls_ctx;
    LONGS_EQUAL(1, vtpe_dispatch_init(&hls_ctx, VTPE_BACKEND_KIT_HLS_TILE, &reader));
    LONGS_EQUAL(
        VTPE_DISPATCH_OK,
        vtpe_scheduler_run_layer_matmul(&reader, &hls_ctx, 0, act_in, hls_acc, hls_scaled));
    LONGS_EQUAL(setun_acc[0], hls_acc[0]);
    LONGS_EQUAL(setun_acc[1], hls_acc[1]);
    LONGS_EQUAL(setun_scaled[0], hls_scaled[0]);
    LONGS_EQUAL(setun_scaled[1], hls_scaled[1]);
    LONGS_EQUAL(0u, hls_ctx.hls_regs.dsp_blocks_used);
    LONGS_EQUAL(2u, hls_ctx.hls_regs.tiles_dispatched);
}

TEST(VeriStreamRuntimeSliceGroup, ExtendedTapeScheduleAndHostOnlyFp32AuxEnforcement) {
    std::vector<uint8_t> ext_tape = read_fixture("fixtures/vtpe_v1/accept/extended_with_fp32_aux.bin");
    std::vector<uint8_t> core_manifest_tape =
        read_fixture("fixtures/vtpe_v1/accept/core_with_manifest_and_links.bin");

    struct vtpe_reader core_m_reader;
    LONGS_EQUAL(
        VTPE_OK,
        vtpe_reader_open(
            &core_m_reader,
            core_manifest_tape.data(),
            core_manifest_tape.size(),
            VTPE_PROFILE_CORE));
    CHECK(core_m_reader.manifest.data != nullptr);
    CHECK(core_m_reader.link_table.data != nullptr);

    struct vtpe_reader reader;
    LONGS_EQUAL(
        VTPE_OK,
        vtpe_reader_open(&reader, ext_tape.data(), ext_tape.size(), VTPE_PROFILE_EXTENDED));
    LONGS_EQUAL(1u, reader.profile);
    LONGS_EQUAL(3u, reader.layer_count);
    LONGS_EQUAL(2u, reader.fp32_aux_count);

    // Build KERNEL_SPEC schedule plan with tile_k = 4
    struct vtpe_schedule_plan plan;
    LONGS_EQUAL(1, vtpe_scheduler_build_plan(&reader, 4, &plan));
    LONGS_EQUAL(3u, plan.count);

    // Layer 0: host_embedding (host-consumed, never hardware-visible)
    LONGS_EQUAL(VTPE_SCHED_OP_HOST_FP32_AUX, plan.steps[0].op_kind);
    LONGS_EQUAL(0u, plan.steps[0].is_hardware_visible);
    // Layer 1: rmsnorm_shift (host-consumed)
    LONGS_EQUAL(VTPE_SCHED_OP_HOST_FP32_AUX, plan.steps[1].op_kind);
    LONGS_EQUAL(0u, plan.steps[1].is_hardware_visible);
    // Layer 2: ternary_dense (hardware-dispatched)
    LONGS_EQUAL(VTPE_SCHED_OP_HW_TERNARY_MATMUL, plan.steps[2].op_kind);
    LONGS_EQUAL(1u, plan.steps[2].is_hardware_visible);
    LONGS_EQUAL(2u, plan.steps[2].num_tiles); // rows=2, cols=4, tile_k=4 -> 2 tiles

    // Execute host fp32_aux stages for layers 0 and 1
    // Tensor 0 (layer 0, host_embedding): 8 floats [0.25, -0.5, 0.75, 1.0, -0.25, 0.5, -0.75, 0.125]
    const float host_in8[8] = {2.0f, 4.0f, 2.0f, 4.0f, 2.0f, 4.0f, 2.0f, 8.0f};
    float emb_out[8] = {0.0f};
    LONGS_EQUAL(1, vtpe_scheduler_run_host_fp32_aux(&reader, 0, host_in8, 8, emb_out));
    DOUBLES_EQUAL(2.0f * 0.25f, emb_out[0], 1e-6);
    DOUBLES_EQUAL(4.0f * -0.5f, emb_out[1], 1e-6);

    // Tensor 1 (layer 1, rmsnorm_shift): 4 floats [1.0, 1.0, 1.0, 1.0]
    const float rms_in4[4] = {1.5f, -2.5f, 3.5f, 4.5f};
    float rms_out[4] = {0.0f};
    LONGS_EQUAL(1, vtpe_scheduler_run_host_fp32_aux(&reader, 1, rms_in4, 4, rms_out));
    DOUBLES_EQUAL(1.5f, rms_out[0], 1e-6);
    DOUBLES_EQUAL(-2.5f, rms_out[1], 1e-6);

    // Execute Layer 2 (2x4 ternary_dense, scale_exp = -2) on setun accelerator
    const int32_t act4[4] = {12, 20, 28, 36};
    int32_t acc2[2] = {0, 0};
    int32_t scaled2[2] = {0, 0};
    struct vtpe_dispatch_ctx setun_ctx;
    LONGS_EQUAL(1, vtpe_dispatch_init(&setun_ctx, VTPE_BACKEND_SETUN_LITEX, &reader));
    LONGS_EQUAL(
        VTPE_DISPATCH_OK,
        vtpe_scheduler_run_layer_matmul(&reader, &setun_ctx, 2, act4, acc2, scaled2));
    LONGS_EQUAL(28, acc2[0]);
    LONGS_EQUAL(36, acc2[1]);
    LONGS_EQUAL(7, scaled2[0]);
    LONGS_EQUAL(9, scaled2[1]);

    // Kit data-flow rule enforcement: attempting to map any fp32_aux bytes into a
    // hardware buffer descriptor MUST be rejected with VTPE_DISPATCH_ERR_HOST_ONLY_AUX_FORBIDDEN!
    struct vtpe_fp32_aux_tensor_view aux0;
    LONGS_EQUAL(1, vtpe_reader_get_fp32_aux(&reader, 0, &aux0));
    float elem0 = 0.0f;
    LONGS_EQUAL(1, vtpe_reader_read_fp32_elem(&aux0, 0, &elem0));
    DOUBLES_EQUAL(0.25f, elem0, 1e-6);

    LONGS_EQUAL(1, vtpe_reader_is_host_only_ptr(&reader, aux0.raw_le_f32, 4));
    LONGS_EQUAL(0, vtpe_reader_is_host_only_ptr(&reader, reader.weights.data, 1));

    struct vtpe_hw_buffer_desc forbidden_desc;
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
        VTPE_DISPATCH_ERR_HOST_ONLY_AUX_FORBIDDEN,
        vtpe_dispatch_ternary_matmul(&setun_ctx, &forbidden_desc));
}

TEST(VeriStreamRuntimeSliceGroup, ReaderDispatchAndSchedulerDefensiveEdgeBranches) {
    std::vector<uint8_t> core_tape = read_fixture("fixtures/vtpe_v1/accept/core_minimal_dot4.bin");
    std::vector<uint8_t> bad_magic = read_fixture("fixtures/vtpe_v1/reject/bad_magic.bin");

    struct vtpe_reader reader;
    // Null out pointer or invalid tape
    LONGS_EQUAL(
        VTPE_ERR_NULL_OR_TRUNCATED,
        vtpe_reader_open(nullptr, core_tape.data(), core_tape.size(), VTPE_PROFILE_CORE));
    LONGS_EQUAL(
        VTPE_ERR_BAD_MAGIC,
        vtpe_reader_open(&reader, bad_magic.data(), bad_magic.size(), VTPE_PROFILE_CORE));

    LONGS_EQUAL(
        VTPE_OK,
        vtpe_reader_open(&reader, core_tape.data(), core_tape.size(), VTPE_PROFILE_CORE));

    // Reader bounds / null checks
    struct vtpe_layer_desc ldesc;
    LONGS_EQUAL(0, vtpe_reader_get_layer(nullptr, 0, &ldesc));
    LONGS_EQUAL(0, vtpe_reader_get_layer(&reader, 0, nullptr));
    LONGS_EQUAL(0, vtpe_reader_get_layer(&reader, 99, &ldesc));

    int16_t sc = 0;
    LONGS_EQUAL(0, vtpe_reader_get_scale_exp(nullptr, 0, &sc));
    LONGS_EQUAL(0, vtpe_reader_get_scale_exp(&reader, 0, nullptr));
    LONGS_EQUAL(0, vtpe_reader_get_scale_exp(&reader, 99, &sc));

    struct vtpe_fp32_aux_tensor_view aux_view;
    LONGS_EQUAL(0, vtpe_reader_get_fp32_aux(nullptr, 0, &aux_view));
    LONGS_EQUAL(0, vtpe_reader_get_fp32_aux(&reader, 0, nullptr));
    LONGS_EQUAL(0, vtpe_reader_get_fp32_aux(&reader, 0, &aux_view)); // core tape has 0 aux

    std::vector<uint8_t> ext_tape = read_fixture("fixtures/vtpe_v1/accept/extended_with_fp32_aux.bin");
    struct vtpe_reader ext_reader;
    LONGS_EQUAL(
        VTPE_OK,
        vtpe_reader_open(&ext_reader, ext_tape.data(), ext_tape.size(), VTPE_PROFILE_EXTENDED));
    LONGS_EQUAL(0, vtpe_reader_get_fp32_aux(&ext_reader, 99, &aux_view));
    LONGS_EQUAL(1, vtpe_reader_get_fp32_aux(&ext_reader, 1, &aux_view));

    float fval = 0.0f;
    LONGS_EQUAL(0, vtpe_reader_read_fp32_elem(nullptr, 0, &fval));
    LONGS_EQUAL(0, vtpe_reader_read_fp32_elem(&aux_view, 0, nullptr));
    LONGS_EQUAL(0, vtpe_reader_read_fp32_elem(&aux_view, 99, &fval));

    LONGS_EQUAL(0, vtpe_reader_is_host_only_ptr(nullptr, core_tape.data(), 4));
    LONGS_EQUAL(0, vtpe_reader_is_host_only_ptr(&reader, core_tape.data(), 4));
    LONGS_EQUAL(0, vtpe_reader_is_host_only_ptr(&ext_reader, nullptr, 4));
    LONGS_EQUAL(0, vtpe_reader_is_host_only_ptr(&ext_reader, ext_tape.data(), 4));
    LONGS_EQUAL(
        0,
        vtpe_reader_is_host_only_ptr(
            &ext_reader,
            ext_reader.fp32_aux.data + ext_reader.fp32_aux.length,
            4));

    // Dispatch defensive checks
    struct vtpe_dispatch_ctx dctx;
    LONGS_EQUAL(0, vtpe_dispatch_init(nullptr, VTPE_BACKEND_SETUN_LITEX, &reader));
    LONGS_EQUAL(0, vtpe_dispatch_init(&dctx, static_cast<enum vtpe_backend_kind>(99), &reader));
    LONGS_EQUAL(1, vtpe_dispatch_init(&dctx, VTPE_BACKEND_SETUN_LITEX, nullptr));

    const int32_t act4[4] = {1, 2, 3, 4};
    int32_t acc2[2] = {0, 0};
    int32_t sc2[2] = {0, 0};
    struct vtpe_hw_buffer_desc desc;
    std::memset(&desc, 0, sizeof(desc));
    desc.weight_bytes = reader.weights.data;
    desc.weight_len = reader.weights.length;
    desc.act_in = act4;
    desc.rows = 2;
    desc.cols = 4;
    desc.scale_exp = 1; // test positive scale_exp shift as well
    desc.acc_out = acc2;
    desc.scaled_out = sc2;
    LONGS_EQUAL(VTPE_DISPATCH_OK, vtpe_dispatch_ternary_matmul(&dctx, &desc));

    LONGS_EQUAL(VTPE_DISPATCH_ERR_INVALID_ARG, vtpe_dispatch_ternary_matmul(nullptr, &desc));
    LONGS_EQUAL(VTPE_DISPATCH_ERR_INVALID_ARG, vtpe_dispatch_ternary_matmul(&dctx, nullptr));

    struct vtpe_hw_buffer_desc bad_desc = desc;
    bad_desc.weight_bytes = nullptr;
    LONGS_EQUAL(VTPE_DISPATCH_ERR_INVALID_ARG, vtpe_dispatch_ternary_matmul(&dctx, &bad_desc));
    bad_desc = desc;
    bad_desc.act_in = nullptr;
    LONGS_EQUAL(VTPE_DISPATCH_ERR_INVALID_ARG, vtpe_dispatch_ternary_matmul(&dctx, &bad_desc));
    bad_desc = desc;
    bad_desc.acc_out = nullptr;
    LONGS_EQUAL(VTPE_DISPATCH_ERR_INVALID_ARG, vtpe_dispatch_ternary_matmul(&dctx, &bad_desc));
    bad_desc = desc;
    bad_desc.scaled_out = nullptr;
    LONGS_EQUAL(VTPE_DISPATCH_ERR_INVALID_ARG, vtpe_dispatch_ternary_matmul(&dctx, &bad_desc));

    struct vtpe_dispatch_ctx bad_backend_ctx = dctx;
    bad_backend_ctx.backend = static_cast<enum vtpe_backend_kind>(77);
    LONGS_EQUAL(
        VTPE_DISPATCH_ERR_INVALID_ARG,
        vtpe_dispatch_ternary_matmul(&bad_backend_ctx, &desc));

    bad_desc = desc;
    bad_desc.rows = 0;
    LONGS_EQUAL(VTPE_DISPATCH_ERR_DIM_MISMATCH, vtpe_dispatch_ternary_matmul(&dctx, &bad_desc));
    bad_desc = desc;
    bad_desc.cols = 0;
    LONGS_EQUAL(VTPE_DISPATCH_ERR_DIM_MISMATCH, vtpe_dispatch_ternary_matmul(&dctx, &bad_desc));
    bad_desc = desc;
    bad_desc.cols = 3;
    LONGS_EQUAL(VTPE_DISPATCH_ERR_DIM_MISMATCH, vtpe_dispatch_ternary_matmul(&dctx, &bad_desc));
    bad_desc = desc;
    bad_desc.weight_len = 0;
    LONGS_EQUAL(VTPE_DISPATCH_ERR_DIM_MISMATCH, vtpe_dispatch_ternary_matmul(&dctx, &bad_desc));

    // Reserved trit 0b11 in weights rejected by dispatch
    const uint8_t bad_trit_weight[2] = {0x03u, 0x00u};
    bad_desc = desc;
    bad_desc.weight_bytes = bad_trit_weight;
    bad_desc.weight_len = 2;
    LONGS_EQUAL(VTPE_DISPATCH_ERR_RESERVED_TRIT, vtpe_dispatch_ternary_matmul(&dctx, &bad_desc));

    // Scheduler defensive branches
    struct vtpe_schedule_plan plan;
    LONGS_EQUAL(0, vtpe_scheduler_build_plan(nullptr, 4, &plan));
    LONGS_EQUAL(0, vtpe_scheduler_build_plan(&reader, 4, nullptr));
    LONGS_EQUAL(0, vtpe_scheduler_build_plan(&reader, 0, &plan));
    LONGS_EQUAL(0, vtpe_scheduler_build_plan(&reader, 3, &plan)); // tile_k not multiple of 4

    struct vtpe_reader huge_layer_reader = reader;
    huge_layer_reader.layer_count = 17;
    LONGS_EQUAL(0, vtpe_scheduler_build_plan(&huge_layer_reader, 4, &plan));

    LONGS_EQUAL(
        VTPE_DISPATCH_ERR_INVALID_ARG,
        vtpe_scheduler_run_layer_matmul(nullptr, &dctx, 0, act4, acc2, sc2));
    LONGS_EQUAL(
        VTPE_DISPATCH_ERR_INVALID_ARG,
        vtpe_scheduler_run_layer_matmul(&reader, nullptr, 0, act4, acc2, sc2));
    LONGS_EQUAL(
        VTPE_DISPATCH_ERR_INVALID_ARG,
        vtpe_scheduler_run_layer_matmul(&reader, &dctx, 99, act4, acc2, sc2));
    // Layer 0 in ext_reader is host_embedding, not a hardware ternary matmul layer
    LONGS_EQUAL(
        VTPE_DISPATCH_ERR_HOST_ONLY_AUX_FORBIDDEN,
        vtpe_scheduler_run_layer_matmul(&ext_reader, &dctx, 0, act4, acc2, sc2));

    float hin[8] = {1.0f};
    float hout[8] = {0.0f};
    LONGS_EQUAL(0, vtpe_scheduler_run_host_fp32_aux(nullptr, 0, hin, 8, hout));
    LONGS_EQUAL(0, vtpe_scheduler_run_host_fp32_aux(&ext_reader, 0, nullptr, 8, hout));
    LONGS_EQUAL(0, vtpe_scheduler_run_host_fp32_aux(&ext_reader, 0, hin, 8, nullptr));
    LONGS_EQUAL(0, vtpe_scheduler_run_host_fp32_aux(&ext_reader, 2, hin, 8, hout)); // layer 2 is hw matmul
    LONGS_EQUAL(0, vtpe_scheduler_run_host_fp32_aux(&ext_reader, 0, hin, 3, hout)); // elem count mismatch
}
