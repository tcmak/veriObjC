#ifndef VERIOBJC_RT_DISPATCH_H
#define VERIOBJC_RT_DISPATCH_H

#include "rt/vtpe_reader.h"

#ifdef __cplusplus
extern "C" {
#endif

enum vtpe_backend_kind {
    VTPE_BACKEND_SETUN_LITEX = 1,
    VTPE_BACKEND_KIT_HLS_TILE = 2
};

enum vtpe_dispatch_status {
    VTPE_DISPATCH_OK = 0,
    VTPE_DISPATCH_ERR_INVALID_ARG = 1,
    VTPE_DISPATCH_ERR_HOST_ONLY_AUX_FORBIDDEN = 2,
    VTPE_DISPATCH_ERR_DIM_MISMATCH = 3,
    VTPE_DISPATCH_ERR_RESERVED_TRIT = 4
};

struct vtpe_setun_csr_regs {
    uint32_t csr_ctrl;
    uint32_t csr_status;
    uint32_t dma_weight_offset;
    uint32_t dma_rows;
    uint32_t dma_cols;
    uint32_t irq_count;
    uint32_t cycles_elapsed;
};

struct vtpe_hls_tile_regs {
    uint32_t ap_ctrl;
    uint16_t tile_m;
    uint16_t tile_k;
    uint32_t tiles_dispatched;
    uint32_t dsp_blocks_used;
    uint32_t cycles_elapsed;
};

struct vtpe_hw_buffer_desc {
    const uint8_t *weight_bytes;
    uint32_t weight_len;
    const int32_t *act_in;
    uint16_t rows;
    uint16_t cols;
    int16_t scale_exp;
    int32_t *acc_out;
    int32_t *scaled_out;
};

struct vtpe_dispatch_ctx {
    enum vtpe_backend_kind backend;
    const struct vtpe_reader *bound_reader;
    struct vtpe_setun_csr_regs setun_regs;
    struct vtpe_hls_tile_regs hls_regs;
};

static inline int vtpe_dispatch_init(
    struct vtpe_dispatch_ctx *ctx,
    enum vtpe_backend_kind backend,
    const struct vtpe_reader *reader) {
    if (!ctx || (backend != VTPE_BACKEND_SETUN_LITEX && backend != VTPE_BACKEND_KIT_HLS_TILE)) {
        return 0;
    }
    __builtin_memset(ctx, 0, sizeof(*ctx));
    ctx->backend = backend;
    ctx->bound_reader = reader;
    return 1;
}

static inline enum vtpe_dispatch_status vtpe_dispatch_ternary_matmul(
    struct vtpe_dispatch_ctx *ctx,
    const struct vtpe_hw_buffer_desc *desc) {
    if (!ctx || !desc || !desc->weight_bytes || !desc->act_in || !desc->acc_out || !desc->scaled_out) {
        return VTPE_DISPATCH_ERR_INVALID_ARG;
    }
    if (ctx->backend != VTPE_BACKEND_SETUN_LITEX && ctx->backend != VTPE_BACKEND_KIT_HLS_TILE) {
        return VTPE_DISPATCH_ERR_INVALID_ARG;
    }
    if (desc->rows == 0u || desc->cols == 0u || (desc->cols & 3u) != 0u) {
        return VTPE_DISPATCH_ERR_DIM_MISMATCH;
    }
    uint32_t row_bytes = (uint32_t)desc->cols >> 2u;
    uint32_t needed_bytes = (uint32_t)desc->rows * row_bytes;
    if (desc->weight_len < needed_bytes) {
        return VTPE_DISPATCH_ERR_DIM_MISMATCH;
    }
    if (vtpe_reader_is_host_only_ptr(ctx->bound_reader, desc->weight_bytes, desc->weight_len)) {
        return VTPE_DISPATCH_ERR_HOST_ONLY_AUX_FORBIDDEN;
    }

    for (uint16_t r = 0u; r < desc->rows; ++r) {
        int32_t acc = 0;
        const uint8_t *row_ptr = desc->weight_bytes + (uint32_t)r * row_bytes;
        for (uint16_t c = 0u; c < desc->cols; ++c) {
            uint8_t b = row_ptr[c >> 2u];
            uint8_t code = (uint8_t)((b >> ((c & 3u) * 2u)) & 0x03u);
            if (code == 0x03u) {
                return VTPE_DISPATCH_ERR_RESERVED_TRIT;
            }
            if (code == 0x01u) {
                acc += desc->act_in[c];
            } else if (code == 0x02u) {
                acc -= desc->act_in[c];
            }
        }
        desc->acc_out[r] = acc;
        if (desc->scale_exp >= 0) {
            desc->scaled_out[r] = acc << (uint32_t)desc->scale_exp;
        } else {
            desc->scaled_out[r] = acc >> (uint32_t)(-desc->scale_exp);
        }
    }

    uint32_t tiles = (uint32_t)desc->rows * row_bytes;
    if (ctx->backend == VTPE_BACKEND_SETUN_LITEX) {
        ctx->setun_regs.csr_ctrl = 0x1u;
        ctx->setun_regs.csr_status = 0x2u; // DONE
        ctx->setun_regs.dma_rows = desc->rows;
        ctx->setun_regs.dma_cols = desc->cols;
        ctx->setun_regs.irq_count += 1u;
        ctx->setun_regs.cycles_elapsed += tiles * 2u;
    } else {
        ctx->hls_regs.ap_ctrl = 0x6u; // ap_done | ap_idle
        ctx->hls_regs.tile_m = 1u;
        ctx->hls_regs.tile_k = 4u;
        ctx->hls_regs.tiles_dispatched += tiles;
        ctx->hls_regs.dsp_blocks_used = 0u;
        ctx->hls_regs.cycles_elapsed += tiles * 2u;
    }

    return VTPE_DISPATCH_OK;
}

#ifdef __cplusplus
}
#endif

#endif /* VERIOBJC_RT_DISPATCH_H */
