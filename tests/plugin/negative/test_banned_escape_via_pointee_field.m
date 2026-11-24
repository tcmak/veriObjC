// Adversarial negative plugin test (Task 6.0.6):
// Even when a cursor struct is allocated on an inner data stack, if its pointer field
// is populated from inner stack memory (directly or via an inline C99 unpacker),
// extracting cursor->payload into an outer slice MUST still be caught as a stack escape.
#include "veriobjc_runtime.h"
#include "vstd/span.h"
#include <synth_c99_frame.h>

vspan_def(EscapingPayloadSpan, const uint8_t);

void illegal_escape_stack_bytes_through_c99_cursor(
    struct veriobjc_stack *scratch_stack,
    EscapingPayloadSpan *out_span
) {
    EscapingPayloadSpan outer_slice = { .data = NULL, .len = 0u };
    {
        VERIOBJC_SCOPED_STACK(scratch_stack);
        uint8_t *stack_wire_bytes = (uint8_t *)veriobjc_stack_alloc(scratch_stack, 32u);
        struct synth_c99_frame *frame =
            (struct synth_c99_frame *)veriobjc_stack_alloc(scratch_stack, sizeof(*frame));
        if (stack_wire_bytes && frame &&
            synth_c99_unpack_frame(stack_wire_bytes, 32u, frame)) {
            // frame->payload points into stack_wire_bytes (depth 2) — MUST fail closed!
            vspan_init(&outer_slice, frame->payload, frame->payload_len);
        }
    }
    if (out_span) {
        *out_span = outer_slice;
    }
}
