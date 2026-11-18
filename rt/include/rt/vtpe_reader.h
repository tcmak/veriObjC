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
    if (!out) {
        return VTPE_ERR_NULL_OR_TRUNCATED;
    }
    vtpe_v1_error_t st = vtpe_v1_validate_tape(buf, len, reader_profile);
    if (st != VTPE_OK) {
        return st;
    }

    __builtin_memset(out, 0, sizeof(*out));
    struct vtpe_v1_header hdr;
    __builtin_memcpy(&hdr, buf, sizeof(hdr));

    out->tape_base = buf;
    out->tape_size = len;
    out->flags = hdr.flags;
    out->profile = (hdr.flags & VTPE_V1_FLAG_EXTENDED) ? 1u : 0u;
    out->section_count = hdr.section_count;
    out->total_length = hdr.total_length;
    out->header_fnv1a = hdr.header_fnv1a;
    out->artifact_id = hdr.artifact_id;

    size_t off = sizeof(struct vtpe_v1_header);
    for (uint8_t i = 0u; i < hdr.section_count; ++i) {
        struct vtpe_v1_section_header shdr;
        __builtin_memcpy(&shdr, buf + off, sizeof(shdr));
        const uint8_t *p_ptr = buf + off + sizeof(shdr);
        if (shdr.tag == VTPE_V1_TAG_WEIGHTS) {
            out->weights.data = p_ptr;
            out->weights.length = shdr.length;
        } else if (shdr.tag == VTPE_V1_TAG_SCALES) {
            out->scales.data = p_ptr;
            out->scales.length = shdr.length;
        } else if (shdr.tag == VTPE_V1_TAG_ARCH_DESCRIPTOR) {
            out->arch_descriptor.data = p_ptr;
            out->arch_descriptor.length = shdr.length;
        } else if (shdr.tag == VTPE_V1_TAG_MANIFEST) {
            out->manifest.data = p_ptr;
            out->manifest.length = shdr.length;
        } else if (shdr.tag == VTPE_V1_TAG_LINK_TABLE) {
            out->link_table.data = p_ptr;
            out->link_table.length = shdr.length;
        } else {
            out->fp32_aux.data = p_ptr;
            out->fp32_aux.length = shdr.length;
        }
        off += sizeof(shdr) + (size_t)shdr.length;
    }

    __builtin_memcpy(&out->layer_count, out->arch_descriptor.data, 2u);
    if (out->fp32_aux.data) {
        __builtin_memcpy(&out->fp32_aux_count, out->fp32_aux.data, 2u);
    }
    return VTPE_OK;
}

static inline int vtpe_reader_get_layer(
    const struct vtpe_reader *r,
    uint16_t layer_idx,
    struct vtpe_layer_desc *out_layer) {
    if (!r || !out_layer || layer_idx >= r->layer_count) {
        return 0;
    }

    uint32_t w_off = 0u;
    uint16_t dense_idx = 0u;
    for (uint16_t i = 0u; i <= layer_idx; ++i) {
        struct vtpe_v1_arch_layer al;
        __builtin_memcpy(&al, r->arch_descriptor.data + 4u + 16u * (uint32_t)i, sizeof(al));
        if (i == layer_idx) {
            out_layer->layer_kind = al.layer_kind;
            out_layer->dim_in = al.dim_in;
            out_layer->dim_out = al.dim_out;
            out_layer->reduce_stages = al.reduce_stages;
            out_layer->latency_cycles = al.latency_cycles;
            out_layer->context_cap = al.context_cap;
            out_layer->weight_byte_offset = w_off;
            out_layer->scale_index = dense_idx;
        }
        uint32_t is_dense = (al.layer_kind == VTPE_LAYER_TERNARY_DENSE) ? 1u : 0u;
        w_off += is_dense * (uint32_t)al.dim_out * (((uint32_t)al.dim_in + 3u) / 4u);
        dense_idx = (uint16_t)(dense_idx + is_dense);
    }
    return 1;
}

static inline int vtpe_reader_get_scale_exp(
    const struct vtpe_reader *r,
    uint16_t scale_idx,
    int16_t *out_exp) {
    if (!r || !out_exp || (uint32_t)scale_idx * 2u + 2u > r->scales.length) {
        return 0;
    }
    __builtin_memcpy(out_exp, r->scales.data + (uint32_t)scale_idx * 2u, 2u);
    return 1;
}

static inline int vtpe_reader_get_fp32_aux(
    const struct vtpe_reader *r,
    uint16_t aux_idx,
    struct vtpe_fp32_aux_tensor_view *out_view) {
    if (!r || !out_view || aux_idx >= r->fp32_aux_count) {
        return 0;
    }
    uint32_t off = 4u;
    for (uint16_t i = 0u; i <= aux_idx; ++i) {
        struct vtpe_v1_fp32_aux_record rec;
        __builtin_memcpy(&rec, r->fp32_aux.data + off, sizeof(rec));
        if (i == aux_idx) {
            out_view->layer_index = rec.layer_index;
            out_view->aux_role = rec.aux_role;
            out_view->elem_count = rec.elem_count;
            out_view->raw_le_f32 = r->fp32_aux.data + off + sizeof(rec);
        }
        off += (uint32_t)sizeof(rec) + rec.elem_count * 4u;
    }
    return 1;
}

static inline int vtpe_reader_read_fp32_elem(
    const struct vtpe_fp32_aux_tensor_view *view,
    uint32_t elem_idx,
    float *out_val) {
    if (!view || !out_val || elem_idx >= view->elem_count) {
        return 0;
    }
    __builtin_memcpy(out_val, view->raw_le_f32 + elem_idx * 4u, 4u);
    return 1;
}

static inline int vtpe_reader_is_host_only_ptr(
    const struct vtpe_reader *r,
    const void *ptr,
    size_t byte_len) {
    if (!r || !ptr || !r->fp32_aux.data) {
        return 0;
    }
    uintptr_t aux_lo = (uintptr_t)r->fp32_aux.data;
    uintptr_t aux_hi = aux_lo + (uintptr_t)r->fp32_aux.length;
    uintptr_t p_lo = (uintptr_t)ptr;
    uintptr_t p_hi = p_lo + (uintptr_t)byte_len;
    if (p_hi > aux_lo && p_lo < aux_hi) {
        return 1;
    }
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* VERIOBJC_RT_VTPE_READER_H */
