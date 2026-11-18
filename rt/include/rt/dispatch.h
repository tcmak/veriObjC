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
    (void)ctx;
    (void)backend;
    (void)reader;
    return 0;
}

static inline enum vtpe_dispatch_status vtpe_dispatch_ternary_matmul(
    struct vtpe_dispatch_ctx *ctx,
    const struct vtpe_hw_buffer_desc *desc) {
    (void)ctx;
    (void)desc;
    return VTPE_DISPATCH_ERR_INVALID_ARG;
}

#ifdef __cplusplus
}
#endif

#endif /* VERIOBJC_RT_DISPATCH_H */
