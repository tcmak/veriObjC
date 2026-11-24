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

//Combinational (0-cycle) GLU mux gate: selects main_val when gate_sign > 0
static inline int32_t vternary_glu_mux_gate(int32_t main_val, int32_t gate_sign) {
    return (gate_sign > 0) ? main_val : 0;
}

// Round-half-up arithmetic right shift 
//   shift == 0 -> val
//   shift > 0  -> (val + (1 << (shift - 1))) >> shift
static inline int32_t vternary_round_shift_right(int64_t val, uint32_t shift) {
    uint32_t sh = shift & 31u;
    if (sh == 0u) {
        return (int32_t)val;
    }
    int64_t bias = 1LL << (sh - 1u);
    return (int32_t)((val + bias) >> sh);
}

// Shift-only power-of-two RMSNorm scaling with round-half-up (zero multipliers)
static inline int32_t vternary_rmsnorm_shift(int32_t val, uint32_t shift_bits) {
    return vternary_round_shift_right((int64_t)val, shift_bits);
}

// Pure CPU software fallback 512-lane ternary dot product with round-half-up right shift.
// Note: Hardware RTL/CSR definitions and co-simulation oracles.
static inline int32_t veriobjc_ternary_dot_512(
    const int8_t *activations,
    const uint8_t *packed_weights_128,
    uint32_t shift
) {
    if (!activations || !packed_weights_128) {
        return 0;
    }
    int32_t acc = 0;
    for (uint32_t byte_idx = 0u; byte_idx < 128u; ++byte_idx) {
        uint8_t packed = packed_weights_128[byte_idx];
        for (uint32_t sub = 0u; sub < 4u; ++sub) {
            uint8_t code = (uint8_t)((packed >> (sub << 1u)) & 0x3u);
            int8_t trit = (int8_t)veriobjc_cmux(code == 1u, 1, veriobjc_cmux(code == 2u, -1, 0));
            acc += veriobjc_ternary_select(trit, activations[(byte_idx << 2u) + sub]);
        }
    }
    return vternary_round_shift_right((int64_t)acc, shift);
}

#ifdef __cplusplus
}
#endif
