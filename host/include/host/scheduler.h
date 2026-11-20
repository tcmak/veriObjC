#ifndef VERIOBJC_HOST_SCHEDULER_H
#define VERIOBJC_HOST_SCHEDULER_H

#include "vstd/ttape_reader.h"
#include "vstd/ternary.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <host/scheduler.h> — Host-Side TTAPE Layer Planner & CPU Fallback Runner
// Layer: host (depends on: rt, vstd)
// Note: Contains zero hardware CSR structs or RTL co-simulation stand-ins.
// ---------------------------------------------------------------------------

enum ttape_sched_op_kind {
    TTAPE_SCHED_OP_TERNARY_MATMUL = 1,
    TTAPE_SCHED_OP_HOST_FP32_AUX = 2
};

enum ttape_sched_status {
    TTAPE_SCHED_OK = 0,
    TTAPE_SCHED_ERR_INVALID_ARG = 1,
    TTAPE_SCHED_ERR_HOST_ONLY_AUX_FORBIDDEN = 2,
    TTAPE_SCHED_ERR_DIM_MISMATCH = 3,
    TTAPE_SCHED_ERR_RESERVED_TRIT = 4
};

struct ttape_layer_invocation {
    uint16_t layer_index;
    uint16_t layer_kind;
    enum ttape_sched_op_kind op_kind;
    uint16_t rows;
    uint16_t cols;
    uint16_t tile_k;
    uint16_t num_tiles;
    uint32_t weight_byte_offset;
    int16_t scale_exp;
    uint8_t is_hardware_visible;
};

struct ttape_schedule_plan {
    uint16_t count;
    struct ttape_layer_invocation steps[16];
};

struct ttape_cpu_buffer_desc {
    const uint8_t *weight_bytes;
    uint32_t weight_len;
    const int32_t *act_in;
    uint16_t rows;
    uint16_t cols;
    int16_t scale_exp;
    int32_t *acc_out;
    int32_t *scaled_out;
};

static inline int ttape_scheduler_build_plan(
    const struct ttape_reader *r,
    uint16_t tile_k,
    struct ttape_schedule_plan *out_plan
) {
    if (!r || !out_plan || tile_k == 0u || (tile_k & 3u) != 0u || r->layer_count > 16u) {
        return 0;
    }
    __builtin_memset(out_plan, 0, sizeof(*out_plan));
    out_plan->count = r->layer_count;

    for (uint16_t i = 0u; i < r->layer_count; ++i) {
        struct ttape_layer_desc ldesc;
        ttape_reader_get_layer(r, i, &ldesc);
        struct ttape_layer_invocation *step = &out_plan->steps[i];
        step->layer_index = i;
        step->layer_kind = ldesc.layer_kind;
        step->rows = ldesc.dim_out;
        step->cols = ldesc.dim_in;
        step->tile_k = tile_k;
        step->weight_byte_offset = ldesc.weight_byte_offset;

        if (ldesc.layer_kind == TTAPE_LAYER_TERNARY_DENSE) {
            step->op_kind = TTAPE_SCHED_OP_TERNARY_MATMUL;
            step->num_tiles = (uint16_t)(ldesc.dim_out * ((ldesc.dim_in + tile_k - 1u) / tile_k));
            ttape_reader_get_scale_exp(r, ldesc.scale_index, &step->scale_exp);
            step->is_hardware_visible = 1u;
        } else {
            step->op_kind = TTAPE_SCHED_OP_HOST_FP32_AUX;
            step->num_tiles = 0u;
            step->scale_exp = 0;
            step->is_hardware_visible = 0u;
        }
    }
    return 1;
}

static inline enum ttape_sched_status ttape_cpu_run_ternary_matmul(
    const struct ttape_reader *bound_reader,
    const struct ttape_cpu_buffer_desc *desc
) {
    if (!desc || !desc->weight_bytes || !desc->act_in || !desc->acc_out || !desc->scaled_out) {
        return TTAPE_SCHED_ERR_INVALID_ARG;
    }
    if (desc->rows == 0u || desc->cols == 0u) {
        return TTAPE_SCHED_ERR_DIM_MISMATCH;
    }
    uint32_t row_bytes = ((uint32_t)desc->cols + 3u) / 4u;
    uint32_t needed_bytes = (uint32_t)desc->rows * row_bytes;
    if (desc->weight_len < needed_bytes) {
        return TTAPE_SCHED_ERR_DIM_MISMATCH;
    }
    if (ttape_reader_is_host_only_ptr(bound_reader, desc->weight_bytes, desc->weight_len)) {
        return TTAPE_SCHED_ERR_HOST_ONLY_AUX_FORBIDDEN;
    }

    for (uint16_t r = 0u; r < desc->rows; ++r) {
        int64_t acc = 0;
        const uint8_t *row_ptr = desc->weight_bytes + (uint32_t)r * row_bytes;
        for (uint16_t c = 0u; c < desc->cols; ++c) {
            uint8_t b = row_ptr[c >> 2u];
            uint8_t code = (uint8_t)((b >> ((c & 3u) * 2u)) & 0x03u);
            if (code == 0x03u) {
                return TTAPE_SCHED_ERR_RESERVED_TRIT;
            }
            if (code == 0x01u) {
                acc += (int64_t)desc->act_in[c];
            } else if (code == 0x02u) {
                acc -= (int64_t)desc->act_in[c];
            }
        }
        desc->acc_out[r] = (int32_t)acc;
        if (desc->scale_exp >= 0) {
            uint32_t sh = (uint32_t)desc->scale_exp & 31u;
            desc->scaled_out[r] = (int32_t)(acc << sh);
        } else {
            desc->scaled_out[r] = vternary_round_shift_right(acc, (uint32_t)(-desc->scale_exp));
        }
    }
    return TTAPE_SCHED_OK;
}

static inline enum ttape_sched_status ttape_scheduler_run_layer_cpu_fallback(
    const struct ttape_reader *r,
    uint16_t layer_idx,
    const int32_t *act_in,
    int32_t *acc_out,
    int32_t *scaled_out
) {
    if (!r || !act_in || !acc_out || !scaled_out) {
        return TTAPE_SCHED_ERR_INVALID_ARG;
    }
    struct ttape_layer_desc ldesc;
    if (!ttape_reader_get_layer(r, layer_idx, &ldesc)) {
        return TTAPE_SCHED_ERR_INVALID_ARG;
    }
    if (ldesc.layer_kind != TTAPE_LAYER_TERNARY_DENSE) {
        return TTAPE_SCHED_ERR_HOST_ONLY_AUX_FORBIDDEN;
    }

    uint32_t row_bytes = ((uint32_t)ldesc.dim_in + 3u) / 4u;
    const uint8_t *w_base = r->weights.data + ldesc.weight_byte_offset;

    if (r->group_scales_count > 0u) {
        struct ttape_group_scales_view gview;
        ttape_reader_get_group_scales(r, ldesc.scale_index, &gview);
        for (uint16_t row = 0u; row < ldesc.dim_out; ++row) {
            const uint8_t *row_ptr = w_base + (uint32_t)row * row_bytes;
            int64_t raw_acc = 0;
            int64_t row_scaled_acc = 0;
            for (uint16_t g = 0u; g < gview.n_groups; ++g) {
                uint32_t col_start = (uint32_t)g * (uint32_t)gview.group_size;
                uint32_t col_end = col_start + (uint32_t)gview.group_size;
                if (col_end > (uint32_t)ldesc.dim_in) {
                    col_end = (uint32_t)ldesc.dim_in;
                }
                int64_t grp_sum = 0;
                for (uint32_t c = col_start; c < col_end; ++c) {
                    uint8_t code = (uint8_t)((row_ptr[c >> 2u] >> ((c & 3u) * 2u)) & 0x03u);
                    if (code == 0x01u) {
                        grp_sum += (int64_t)act_in[c];
                    } else if (code == 0x02u) {
                        grp_sum -= (int64_t)act_in[c];
                    }
                }
                uint16_t scale_u16 = 0u;
                ttape_reader_read_group_scale_u16(
                    &gview, (uint32_t)row * (uint32_t)gview.n_groups + (uint32_t)g, &scale_u16
                );
                raw_acc += grp_sum;
                row_scaled_acc += grp_sum * (int64_t)scale_u16;
            }
            acc_out[row] = (int32_t)raw_acc;
            scaled_out[row] = vternary_round_shift_right(row_scaled_acc, gview.shift);
        }
        return TTAPE_SCHED_OK;
    }

    int16_t scale_exp = 0;
    ttape_reader_get_scale_exp(r, ldesc.scale_index, &scale_exp);
    struct ttape_cpu_buffer_desc bdesc;
    bdesc.weight_bytes = w_base;
    bdesc.weight_len = (uint32_t)ldesc.dim_out * row_bytes;
    bdesc.act_in = act_in;
    bdesc.rows = ldesc.dim_out;
    bdesc.cols = ldesc.dim_in;
    bdesc.scale_exp = scale_exp;
    bdesc.acc_out = acc_out;
    bdesc.scaled_out = scaled_out;

    return ttape_cpu_run_ternary_matmul(r, &bdesc);
}

static inline int ttape_scheduler_run_host_fp32_aux(
    const struct ttape_reader *r,
    uint16_t layer_idx,
    const float *host_in,
    uint32_t count,
    float *host_out
) {
    if (!r || !host_in || !host_out) {
        return 0;
    }
    for (uint16_t i = 0u; i < r->fp32_aux_count; ++i) {
        struct ttape_fp32_aux_tensor_view view;
        ttape_reader_get_fp32_aux(r, i, &view);
        if (view.layer_index == layer_idx) {
            if (view.elem_count != count) {
                return 0;
            }
            for (uint32_t k = 0u; k < count; ++k) {
                float w = 0.0f;
                ttape_reader_read_fp32_elem(&view, k, &w);
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

#endif /* VERIOBJC_HOST_SCHEDULER_H */
