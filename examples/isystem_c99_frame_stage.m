// examples/isystem_c99_frame_stage.m
// Demonstrates Task 6.0.6 (-isystem freestanding C99 header interop + @cold staging):
//   - Statement-expression endian macros & static inline C99 cursor helper via -isystem
//   - Scratch cursor on VERIOBJC_SCOPED_STACK + validated vspan slice handoff to @pure @nolatency
#include "veriobjc_runtime.h"
#include "veriobjc_host_io.h"
#include "vstd/span.h"
#include <synth_c99_frame.h>

vspan_def(C99FramePayloadSpan, const uint8_t);

static uint8_t g_frame_staging[512];

veriobjc_pure veriobjc_nolatency
uint32_t hash_validated_c99_frame(uint32_t seq_no, C99FramePayloadSpan payload) {
    uint32_t h = 0x811c9dc5u ^ seq_no;
    for (size_t i = 0u; i < payload.len; ++i) {
        const uint8_t *b = vspan_at(&payload, i);
        if (b) {
            h ^= (uint32_t)(*b);
            h = (h << 3u) ^ (h >> 29u);
        }
    }
    return h;
}

veriobjc_cold
int32_t load_and_hash_c99_frame(
    const char *path,
    struct veriobjc_stack *scratch_stack,
    uint32_t *out_hash
) {
    if (!scratch_stack || !out_hash) {
        return -1;
    }

    struct veriobjc_host_io_result staged =
        veriobjc_host_load_file_staged(path, g_frame_staging, sizeof(g_frame_staging));
    if (!staged.is_ok) {
        return -(int32_t)staged.err;
    }
    if (staged.value.len < SYNTH_C99_HEADER_BYTES ||
        SYNTH_C99_READ_U16_LE(staged.value.data) != SYNTH_C99_MAGIC_V1) {
        return -2;
    }

    C99FramePayloadSpan slice = { .data = NULL, .len = 0u };
    uint32_t seq = 0u;
    {
        VERIOBJC_SCOPED_STACK(scratch_stack);
        struct synth_c99_frame *cursor =
            (struct synth_c99_frame *)veriobjc_stack_alloc(scratch_stack, sizeof(*cursor));
        if (!cursor || !synth_c99_unpack_frame(staged.value.data, staged.value.len, cursor)) {
            return -3;
        }
        vspan_init(&slice, cursor->payload, cursor->payload_len);
        seq = cursor->seq_no;
    }

    *out_hash = hash_validated_c99_frame(seq, slice);
    return (int32_t)SYNTH_C99_ALIGN_UP(slice.len, 4u);
}
