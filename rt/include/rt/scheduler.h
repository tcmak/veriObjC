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
    if (!r || !out_plan || tile_k == 0u || (tile_k & 3u) != 0u || r->layer_count > 16u) {
        return 0;
    }
    __builtin_memset(out_plan, 0, sizeof(*out_plan));
    out_plan->count = r->layer_count;

    for (uint16_t i = 0u; i < r->layer_count; ++i) {
        struct vtpe_layer_desc ldesc;
        vtpe_reader_get_layer(r, i, &ldesc);
        struct vtpe_kernel_spec_invocation *step = &out_plan->steps[i];
        step->layer_index = i;
        step->layer_kind = ldesc.layer_kind;
        step->rows = ldesc.dim_out;
        step->cols = ldesc.dim_in;
        step->tile_k = tile_k;
        step->weight_byte_offset = ldesc.weight_byte_offset;

        if (ldesc.layer_kind == VTPE_LAYER_TERNARY_DENSE) {
            step->op_kind = VTPE_SCHED_OP_HW_TERNARY_MATMUL;
            step->num_tiles = (uint16_t)(ldesc.dim_out * ((ldesc.dim_in + tile_k - 1u) / tile_k));
            vtpe_reader_get_scale_exp(r, ldesc.scale_index, &step->scale_exp);
            step->is_hardware_visible = 1u;
        } else {
            step->op_kind = VTPE_SCHED_OP_HOST_FP32_AUX;
            step->num_tiles = 0u;
            step->scale_exp = 0;
            step->is_hardware_visible = 0u;
        }
    }
    return 1;
}

static inline enum vtpe_dispatch_status vtpe_scheduler_run_layer_matmul(
    const struct vtpe_reader *r,
    struct vtpe_dispatch_ctx *dispatch,
    uint16_t layer_idx,
    const int32_t *act_in,
    int32_t *acc_out,
    int32_t *scaled_out) {
    if (!r || !dispatch) {
        return VTPE_DISPATCH_ERR_INVALID_ARG;
    }
    struct vtpe_layer_desc ldesc;
    if (!vtpe_reader_get_layer(r, layer_idx, &ldesc)) {
        return VTPE_DISPATCH_ERR_INVALID_ARG;
    }
    if (ldesc.layer_kind != VTPE_LAYER_TERNARY_DENSE) {
        return VTPE_DISPATCH_ERR_HOST_ONLY_AUX_FORBIDDEN;
    }
    int16_t scale_exp = 0;
    vtpe_reader_get_scale_exp(r, ldesc.scale_index, &scale_exp);

    uint32_t row_bytes = ((uint32_t)ldesc.dim_in + 3u) / 4u;
    struct vtpe_hw_buffer_desc bdesc;
    bdesc.weight_bytes = r->weights.data + ldesc.weight_byte_offset;
    bdesc.weight_len = (uint32_t)ldesc.dim_out * row_bytes;
    bdesc.act_in = act_in;
    bdesc.rows = ldesc.dim_out;
    bdesc.cols = ldesc.dim_in;
    bdesc.scale_exp = scale_exp;
    bdesc.acc_out = acc_out;
    bdesc.scaled_out = scaled_out;

    return vtpe_dispatch_ternary_matmul(dispatch, &bdesc);
}

static inline int vtpe_scheduler_run_host_fp32_aux(
    const struct vtpe_reader *r,
    uint16_t layer_idx,
    const float *host_in,
    uint32_t count,
    float *host_out) {
    if (!r || !host_in || !host_out) {
        return 0;
    }
    for (uint16_t i = 0u; i < r->fp32_aux_count; ++i) {
        struct vtpe_fp32_aux_tensor_view view;
        vtpe_reader_get_fp32_aux(r, i, &view);
        if (view.layer_index == layer_idx) {
            if (view.elem_count != count) {
                return 0;
            }
            for (uint32_t k = 0u; k < count; ++k) {
                float w = 0.0f;
                vtpe_reader_read_fp32_elem(&view, k, &w);
                host_out[k] = host_in[k] * w;
            }
            return 1;
        }
    }
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* VERIOBJC_RT_SCHEDULER_H */
