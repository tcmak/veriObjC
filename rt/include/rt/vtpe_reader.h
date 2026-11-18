#ifndef VERIOBJC_RT_VTPE_READER_H
#define VERIOBJC_RT_VTPE_READER_H

#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

#define VTPE_LAYER_TOKEN_SHIFT       1u
#define VTPE_LAYER_GLU_MUX           2u
#define VTPE_LAYER_RMSNORM_SHIFT     3u
#define VTPE_LAYER_TERNARY_DENSE     4u
#define VTPE_LAYER_KV_ARENA          5u
#define VTPE_LAYER_HOST_EMBEDDING    6u
#define VTPE_LAYER_HOST_SOFTMAX_HEAD 7u

struct vtpe_section_span {
    const uint8_t *data;
    uint32_t length;
};

struct vtpe_layer_desc {
    uint16_t layer_kind;
    uint16_t dim_in;
    uint16_t dim_out;
    uint8_t reduce_stages;
    uint32_t latency_cycles;
    uint32_t context_cap;
    uint32_t weight_byte_offset;
    uint16_t scale_index;
};

struct vtpe_fp32_aux_tensor_view {
    uint16_t layer_index;
    uint8_t aux_role;
    uint32_t elem_count;
    const uint8_t *raw_le_f32;
};

struct vtpe_reader {
    const uint8_t *tape_base;
    size_t tape_size;
    uint8_t flags;
    uint8_t profile;
    uint16_t section_count;
    uint32_t total_length;
    uint32_t header_fnv1a;
    uint64_t artifact_id;
    struct vtpe_section_span weights;
    struct vtpe_section_span scales;
    struct vtpe_section_span arch_descriptor;
    struct vtpe_section_span manifest;
    struct vtpe_section_span link_table;
    struct vtpe_section_span fp32_aux;
    uint16_t layer_count;
    uint16_t fp32_aux_count;
};

static inline vtpe_v1_error_t vtpe_reader_open(
    struct vtpe_reader *out,
    const uint8_t *buf,
    size_t len,
    vtpe_v1_profile_t reader_profile) {
    (void)out;
    (void)buf;
    (void)len;
    (void)reader_profile;
    return VTPE_ERR_NULL_OR_TRUNCATED;
}

static inline int vtpe_reader_get_layer(
    const struct vtpe_reader *r,
    uint16_t layer_idx,
    struct vtpe_layer_desc *out_layer) {
    (void)r;
    (void)layer_idx;
    (void)out_layer;
    return 0;
}

static inline int vtpe_reader_get_scale_exp(
    const struct vtpe_reader *r,
    uint16_t scale_idx,
    int16_t *out_exp) {
    (void)r;
    (void)scale_idx;
    (void)out_exp;
    return 0;
}

static inline int vtpe_reader_get_fp32_aux(
    const struct vtpe_reader *r,
    uint16_t aux_idx,
    struct vtpe_fp32_aux_tensor_view *out_view) {
    (void)r;
    (void)aux_idx;
    (void)out_view;
    return 0;
}

static inline int vtpe_reader_read_fp32_elem(
    const struct vtpe_fp32_aux_tensor_view *view,
    uint32_t elem_idx,
    float *out_val) {
    (void)view;
    (void)elem_idx;
    (void)out_val;
    return 0;
}

static inline int vtpe_reader_is_host_only_ptr(
    const struct vtpe_reader *r,
    const void *ptr,
    size_t byte_len) {
    (void)r;
    (void)ptr;
    (void)byte_len;
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* VERIOBJC_RT_VTPE_READER_H */
