#ifndef SYNTH_C99_FRAME_H
#define SYNTH_C99_FRAME_H

/* Synthetic domain-neutral single-file C99 wire-frame header (Task 6.0.6).
 * Included via -isystem to stress C99 macro/cast/inline interop with VeriObjC.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SYNTH_C99_MAGIC_V1 ((uint16_t)0x564Fu)
#define SYNTH_C99_HEADER_BYTES 8u

/* Statement-expression byte-assembly computation macros (permitted via -isystem,
 * banned if defined directly in user .m translation units).
 */
#define SYNTH_C99_READ_U16_LE(ptr) \
    ({ \
        const uint8_t *_sc_p16 = (const uint8_t *)(ptr); \
        (uint16_t)(((uint16_t)_sc_p16[0]) | (uint16_t)(((uint16_t)_sc_p16[1]) << 8u)); \
    })

#define SYNTH_C99_READ_U32_LE(ptr) \
    ({ \
        const uint8_t *_sc_p32 = (const uint8_t *)(ptr); \
        ((uint32_t)_sc_p32[0]) | \
        (((uint32_t)_sc_p32[1]) << 8u) | \
        (((uint32_t)_sc_p32[2]) << 16u) | \
        (((uint32_t)_sc_p32[3]) << 24u); \
    })

#define SYNTH_C99_ALIGN_UP(val, align_pow2) \
    (((size_t)(val) + ((size_t)(align_pow2) - 1u)) & ~((size_t)(align_pow2) - 1u))

struct synth_c99_frame {
    uint16_t       magic;
    uint16_t       payload_len;
    uint32_t       seq_no;
    const uint8_t *payload;
};

static inline bool synth_c99_unpack_frame(
    const void *in_buf,
    size_t buf_len,
    struct synth_c99_frame *out_frame
) {
    if (!in_buf || !out_frame || buf_len < SYNTH_C99_HEADER_BYTES) {
        return false;
    }
    const uint8_t *bytes = (const uint8_t *)in_buf;
    uint16_t magic = SYNTH_C99_READ_U16_LE(bytes + 0u);
    uint16_t plen  = SYNTH_C99_READ_U16_LE(bytes + 2u);
    uint32_t seq   = SYNTH_C99_READ_U32_LE(bytes + 4u);

    if (magic != SYNTH_C99_MAGIC_V1) {
        return false;
    }
    if ((size_t)SYNTH_C99_HEADER_BYTES + (size_t)plen > buf_len) {
        return false;
    }

    out_frame->magic       = magic;
    out_frame->payload_len = plen;
    out_frame->seq_no      = seq;
    out_frame->payload     = bytes + SYNTH_C99_HEADER_BYTES;
    return true;
}

/* Unannotated external C99 helper: defaults to @cold under VeriObjC FFI rules. */
extern uint32_t synth_c99_external_audit_crc(const void *buf, size_t len);

#endif /* SYNTH_C99_FRAME_H */
