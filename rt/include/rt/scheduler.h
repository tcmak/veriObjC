#ifndef VERIOBJC_RT_SCHEDULER_H
#define VERIOBJC_RT_SCHEDULER_H

#include "rt/dispatch.h"

#ifdef __cplusplus
extern "C" {
#endif

enum vtpe_sched_op_kind {
    VTPE_SCHED_OP_HW_TERNARY_MATMUL = 1,
    VTPE_SCHED_OP_HOST_FP32_AUX = 2
};

struct vtpe_kernel_spec_invocation {
    uint16_t layer_index;
    uint16_t layer_kind;
    enum vtpe_sched_op_kind op_kind;
    uint16_t rows;
    uint16_t cols;
    uint16_t tile_k;
    uint16_t num_tiles;
    uint32_t weight_byte_offset;
    int16_t scale_exp;
    uint8_t is_hardware_visible;
};

struct vtpe_schedule_plan {
    uint16_t count;
    struct vtpe_kernel_spec_invocation steps[16];
};

static inline int vtpe_scheduler_build_plan(
    const struct vtpe_reader *r,
    uint16_t tile_k,
    struct vtpe_schedule_plan *out_plan) {
    (void)r;
    (void)tile_k;
    (void)out_plan;
    return 0;
}

static inline enum vtpe_dispatch_status vtpe_scheduler_run_layer_matmul(
    const struct vtpe_reader *r,
    struct vtpe_dispatch_ctx *dispatch,
    uint16_t layer_idx,
    const int32_t *act_in,
    int32_t *acc_out,
    int32_t *scaled_out) {
    (void)r;
    (void)dispatch;
    (void)layer_idx;
    (void)act_in;
    (void)acc_out;
    (void)scaled_out;
    return VTPE_DISPATCH_ERR_INVALID_ARG;
}

static inline int vtpe_scheduler_run_host_fp32_aux(
    const struct vtpe_reader *r,
    uint16_t layer_idx,
    const float *host_in,
    uint32_t count,
    float *host_out) {
    (void)r;
    (void)layer_idx;
    (void)host_in;
    (void)count;
    (void)host_out;
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* VERIOBJC_RT_SCHEDULER_H */
