#pragma once
#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <vstd/ternary.h> — Packed 2-Bit Ternary Weights & Multiplier-Free ML Blocks
// Layer: vstd (depends on: rt)
//
// Encoding (4 weights per byte, 2 bits per weight):
//   0b00 =  0 (VTERNARY_ZERO)
//   0b01 = +1 (VTERNARY_POS_ONE)
//   0b10 = -1 (VTERNARY_NEG_ONE)
//   0b11 = reserved/invalid (VTERNARY_INVALID — rejected by from_bytes loader)
// ---------------------------------------------------------------------------

enum vternary_code {
    VTERNARY_ZERO    = 0x0u,
    VTERNARY_POS_ONE = 0x1u,
    VTERNARY_NEG_ONE = 0x2u,
    VTERNARY_INVALID = 0x3u
};

static inline uint8_t vternary_pack4(uint8_t w0, uint8_t w1, uint8_t w2, uint8_t w3) {
    return (uint8_t)(((w0 & 0x3u) << 0u) |
                     ((w1 & 0x3u) << 2u) |
                     ((w2 & 0x3u) << 4u) |
                     ((w3 & 0x3u) << 6u));
}

static inline uint8_t vternary_unpack_at(uint8_t packed_byte, uint32_t lane) {
    uint32_t shift = (lane & 0x3u) << 1u;
    return (uint8_t)((packed_byte >> shift) & 0x3u);
}

static inline int32_t vternary_select(int32_t act, uint8_t code) {
    if (code == VTERNARY_POS_ONE) {
        return act;
    }
    if (code == VTERNARY_NEG_ONE) {
        return -act;
    }
    return 0;
}

static inline int32_t vternary_dot_packed(
    const int32_t *activations,
    const uint8_t *packed_weights,
    size_t num_packed_bytes
) {
    if (!activations || !packed_weights) {
        return 0;
    }
    int32_t acc = 0;
    for (size_t b = 0u; b < num_packed_bytes; ++b) {
        uint8_t byte_val = packed_weights[b];
        size_t base_idx = b << 2u;
        acc += vternary_select(activations[base_idx + 0u], vternary_unpack_at(byte_val, 0u));
        acc += vternary_select(activations[base_idx + 1u], vternary_unpack_at(byte_val, 1u));
        acc += vternary_select(activations[base_idx + 2u], vternary_unpack_at(byte_val, 2u));
        acc += vternary_select(activations[base_idx + 3u], vternary_unpack_at(byte_val, 3u));
    }
    return acc;
}

static inline bool vternary_verify_weights_from_bytes(
    const void *tape_buf,
    size_t tape_len,
    uint16_t expected_tag,
    uint32_t expected_weight_bytes,
    const uint8_t **out_weights_ptr
) {
    if (!out_weights_ptr) {
        return false;
    }
    *out_weights_ptr = NULL;
    uint32_t actual_count = 0u;
    const void *payload = veriobjc_from_bytes(tape_buf, tape_len, expected_tag, 1u, &actual_count);
    if (!payload || actual_count != expected_weight_bytes) {
        return false;
    }
    const uint8_t *bytes = (const uint8_t *)payload;
    for (uint32_t i = 0u; i < expected_weight_bytes; ++i) {
        uint8_t b = bytes[i];
        for (uint32_t lane = 0u; lane < 4u; ++lane) {
            if (vternary_unpack_at(b, lane) == VTERNARY_INVALID) {
                return false;
            }
        }
    }
    *out_weights_ptr = bytes;
    return true;
}

// Combinational (0-cycle) GLU mux gate: selects main_val when gate_sign > 0
static inline int32_t vternary_glu_mux_gate(int32_t main_val, int32_t gate_sign) {
    return (gate_sign > 0) ? main_val : 0;
}

// Shift-only power-of-two RMSNorm scaling (zero multipliers / zero DSPs)
static inline int32_t vternary_rmsnorm_shift(int32_t val, uint32_t shift_bits) {
    return val >> (shift_bits & 31u);
}

#ifdef __cplusplus
}
#endif
