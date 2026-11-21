// Task 5.5 End-to-End Composition Test:
//   Mixed-Size @sealed Hierarchy + Heterogeneous Arena Allocation + vspan<Superclass *>
//   Upcast Tag-Switched Dispatch + Zero Ivar Slicing + One-Store Scope Reset
#include "veriobjc_runtime.h"
#include "rt/dispatch.h"
#include "vstd/span.h"

__attribute__((objc_root_class))
__attribute__((objc_sealed))
@interface TelemetryFrameNode {
@public
    int32_t stream_id;
}
- (int64_t)computeDigest;
- (uint32_t)declaredPayloadBytes;
@end

@implementation TelemetryFrameNode
- (int64_t)computeDigest {
    return (int64_t)stream_id;
}
- (uint32_t)declaredPayloadBytes {
    return (uint32_t)sizeof(TelemetryFrameNode);
}
@end

// Subclass 1 (8-byte payload: 4B base + 2B + 2B)
__attribute__((objc_sealed))
__attribute__((objc_final))
@interface CompactHeartbeatNode : TelemetryFrameNode {
@public
    uint16_t seq_no;
    uint16_t status_bits;
}
- (int64_t)computeDigest;
- (uint32_t)declaredPayloadBytes;
@end

@implementation CompactHeartbeatNode
- (int64_t)computeDigest {
    return (int64_t)stream_id * 10 + (int64_t)seq_no + (int64_t)status_bits;
}
- (uint32_t)declaredPayloadBytes {
    return (uint32_t)sizeof(CompactHeartbeatNode);
}
@end

// Subclass 2 (16-byte payload: 4B base + 4B + 4B + 4B)
__attribute__((objc_sealed))
__attribute__((objc_final))
@interface MediumQuoteNode : TelemetryFrameNode {
@public
    int32_t bid_ticks;
    int32_t ask_ticks;
    int32_t lot_size;
}
- (int64_t)computeDigest;
- (uint32_t)declaredPayloadBytes;
@end

@implementation MediumQuoteNode
- (int64_t)computeDigest {
    return (int64_t)stream_id + (int64_t)(ask_ticks - bid_ticks) * (int64_t)lot_size;
}
- (uint32_t)declaredPayloadBytes {
    return (uint32_t)sizeof(MediumQuoteNode);
}
@end

// Subclass 3 (32-byte payload: 4B base + 4B pad + 8B + 8B + 4B + 4B)
__attribute__((objc_sealed))
__attribute__((objc_final))
@interface WideOrderBookNode : TelemetryFrameNode {
@public
    int64_t notional_cents;
    int64_t timestamp_ns;
    int32_t venue_id;
    int32_t depth_levels;
}
- (int64_t)computeDigest;
- (uint32_t)declaredPayloadBytes;
@end

@implementation WideOrderBookNode
- (int64_t)computeDigest {
    return notional_cents + (timestamp_ns >> 2) +
           (int64_t)(stream_id + venue_id + depth_levels);
}
- (uint32_t)declaredPayloadBytes {
    return (uint32_t)sizeof(WideOrderBookNode);
}
@end

_Static_assert(sizeof(TelemetryFrameNode) == 4u, "Expected 4B root payload");
_Static_assert(sizeof(CompactHeartbeatNode) == 8u, "Expected 8B compact subclass payload");
_Static_assert(sizeof(MediumQuoteNode) == 16u, "Expected 16B medium subclass payload");
_Static_assert(sizeof(WideOrderBookNode) == 32u, "Expected 32B wide subclass payload");

vspan_def(TelemetryNodeSpan, TelemetryFrameNode *);

__attribute__((noinline))
static int64_t dispatch_digest_on_base(TelemetryFrameNode *node) {
    if (!node) {
        return -1;
    }
    return [node computeDigest];
}

__attribute__((noinline))
static uint32_t dispatch_size_on_base(TelemetryFrameNode *node) {
    if (!node) {
        return 0u;
    }
    return [node declaredPayloadBytes];
}

// Single-store scope-exit helper so -O2 disassembly exposes the exact stack->top restore
__attribute__((noinline))
void mixed_scope_epilogue_reset(struct veriobjc_stack *s, uint8_t *mark) {
    veriobjc_stack_reset(s, mark);
}

__attribute__((noinline))
int64_t run_mixed_sealed_scope_batch(struct veriobjc_stack *stack, size_t *out_peak_used) {
    if (!stack) {
        return -1;
    }
    size_t entry_used = veriobjc_stack_used(stack);
    int64_t total_digest = 0;

    {
        VERIOBJC_SCOPED_STACK(stack);

        // Interleave heterogeneous concrete sizes: 8B, 32B, 16B, 32B, 8B
        CompactHeartbeatNode *c0 = veriobjc_alloc_sealed(CompactHeartbeatNode, stack);
        WideOrderBookNode    *w1 = veriobjc_alloc_sealed(WideOrderBookNode, stack);
        MediumQuoteNode      *m2 = veriobjc_alloc_sealed(MediumQuoteNode, stack);
        WideOrderBookNode    *w3 = veriobjc_alloc_sealed(WideOrderBookNode, stack);
        CompactHeartbeatNode *c4 = veriobjc_alloc_sealed(CompactHeartbeatNode, stack);
        if (!c0 || !w1 || !m2 || !w3 || !c4) {
            return -2;
        }

        // Initialize later objects first, then write earlier objects to verify zero arena overlap
        m2->stream_id = 3;
        m2->bid_ticks = 100;
        m2->ask_ticks = 105;
        m2->lot_size  = 8; // 3 + 5 * 8 == 43

        w3->stream_id      = 4;
        w3->notional_cents = 1000;
        w3->timestamp_ns   = 40;
        w3->venue_id       = 7;
        w3->depth_levels   = 9; // 1000 + 10 + 20 == 1030

        c4->stream_id   = 5;
        c4->seq_no      = 12u;
        c4->status_bits = 3u; // 50 + 12 + 3 == 65

        c0->stream_id   = 1;
        c0->seq_no      = 20u;
        c0->status_bits = 12u; // 10 + 20 + 12 == 42

        w1->stream_id      = 2;
        w1->notional_cents = 500;
        w1->timestamp_ns   = 80;
        w1->venue_id       = 11;
        w1->depth_levels   = 17; // 500 + 20 + 30 == 550

        if (out_peak_used) {
            *out_peak_used = veriobjc_stack_used(stack);
        }

        // Store heterogeneous subclasses as superclass pointers in a contiguous vspan
        TelemetryFrameNode *raw_nodes[5] = { c0, w1, m2, w3, c4 };
        TelemetryNodeSpan batch_span = vspan_from_array(TelemetryNodeSpan, raw_nodes);

        const int64_t expected_digests[5] = { 42, 550, 43, 1030, 65 };
        const uint32_t expected_widths[5] = { 8u, 32u, 16u, 32u, 8u };

        for (size_t i = 0u; i < batch_span.len; ++i) {
            TelemetryFrameNode **slot = vspan_at(&batch_span, i);
            if (!slot || !*slot) {
                return -3;
            }
            TelemetryFrameNode *base_ref = *slot;
            int64_t got_digest = dispatch_digest_on_base(base_ref);
            uint32_t got_width = dispatch_size_on_base(base_ref);
            if (got_digest != expected_digests[i] || got_width != expected_widths[i]) {
                return -4;
            }
            total_digest += got_digest;
        }
    }

    // VERIOBJC_SCOPED_STACK must have restored stack->top to entry_used in one store
    if (veriobjc_stack_used(stack) != entry_used) {
        return -5;
    }

    return total_digest;
}

int main(void) {
    alignas(16) uint8_t arena_mem[512];
    struct veriobjc_stack stack;
    veriobjc_stack_init(&stack, arena_mem, sizeof(arena_mem));

    // Reserve a 16-byte outer header to verify non-zero scope mark restoration
    uint8_t *outer_mark_before = stack.top;
    void *outer_hdr = veriobjc_stack_alloc(&stack, 16u);
    if (!outer_hdr || veriobjc_stack_used(&stack) != 16u) {
        return 1;
    }

    size_t peak_run1 = 0u;
    size_t peak_run2 = 0u;
    // Expected sum: 42 + 550 + 43 + 1030 + 65 == 1730
    if (run_mixed_sealed_scope_batch(&stack, &peak_run1) != 1730) {
        return 2;
    }
    if (veriobjc_stack_used(&stack) != 16u) {
        return 3;
    }

    // Second batch reuses exact same arena bytes without high-water drift
    if (run_mixed_sealed_scope_batch(&stack, &peak_run2) != 1730) {
        return 4;
    }
    if (peak_run1 == 16u || peak_run1 != peak_run2 || veriobjc_stack_used(&stack) != 16u) {
        return 5;
    }

    mixed_scope_epilogue_reset(&stack, outer_mark_before);
    if (veriobjc_stack_used(&stack) != 0u) {
        return 6;
    }

    return 0;
}
