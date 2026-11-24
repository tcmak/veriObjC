// Positive plugin test (Task 6.0.6):
// Domain-neutral -isystem freestanding C99 header interop with @cold static staging,
// VERIOBJC_SCOPED_STACK scratch allocation, and @pure @nolatency slice handoff.
#include "veriobjc_runtime.h"
#include "veriobjc_host_io.h"
#include "vstd/span.h"
#include <synth_c99_frame.h>

vspan_def(ConstPayloadSpan, const uint8_t);

static uint8_t g_c99_staging_buf[256];

veriobjc_pure veriobjc_nolatency
uint32_t fold_validated_c99_payload(uint32_t seq_no, ConstPayloadSpan payload, size_t aligned_bytes) {
    uint32_t acc = seq_no ^ (uint32_t)aligned_bytes;
    for (size_t i = 0u; i < payload.len; ++i) {
        const uint8_t *b = vspan_at(&payload, i);
        if (b) {
            acc = (acc << 5u) ^ (acc >> 27u) ^ (uint32_t)(*b);
        }
    }
    return acc;
}

veriobjc_cold
int32_t stage_and_validate_c99_frame(
    const char *path,
    struct veriobjc_stack *scratch_stack,
    uint32_t *out_digest
) {
    if (!scratch_stack || !out_digest) {
        return -1;
    }

    struct veriobjc_host_io_result staged =
        veriobjc_host_load_file_staged(path, g_c99_staging_buf, sizeof(g_c99_staging_buf));
    if (!staged.is_ok) {
        return -(int32_t)staged.err;
    }
    if (staged.value.len < SYNTH_C99_HEADER_BYTES) {
        return -10;
    }

    // Invoke -isystem statement-expression macro directly at user call site
    uint16_t quick_magic = SYNTH_C99_READ_U16_LE(staged.value.data);
    if (quick_magic != SYNTH_C99_MAGIC_V1) {
        return -11;
    }

    ConstPayloadSpan validated_slice = { .data = NULL, .len = 0u };
    uint32_t validated_seq = 0u;
    size_t aligned_cap = 0u;

    {
        VERIOBJC_SCOPED_STACK(scratch_stack);
        struct synth_c99_frame *frame =
            (struct synth_c99_frame *)veriobjc_stack_alloc(scratch_stack, sizeof(*frame));
        if (!frame) {
            return -12;
        }
        if (!synth_c99_unpack_frame(staged.value.data, staged.value.len, frame)) {
            return -13;
        }
        // frame->payload aliases static g_c99_staging_buf (not scratch_stack), so
        // extracting flat metadata + bounded slice out of the scratch scope is root-safe.
        vspan_init(&validated_slice, frame->payload, frame->payload_len);
        validated_seq = frame->seq_no;
        aligned_cap   = SYNTH_C99_ALIGN_UP(frame->payload_len, 4u);
    }

    *out_digest = fold_validated_c99_payload(validated_seq, validated_slice, aligned_cap);
    return (int32_t)validated_slice.len;
}
