// examples/cfg_branch_move_router.m
// Demonstrates Task C2.2 (clang::CFG Path-Sensitive Move/Escape & Transitive Zero-DSP Checks):
//   - Branch-symmetric linear move (moving a linear value in both 'then' and 'else' with no post-join use)
//   - Scoped data-stack scratch allocation with non-escaping subobject indexing (&scratch[0])
//   - Transitive zero-DSP helper chain verified across call graph (shifts + CMUX only, 0 multipliers)
#include "veriobjc_runtime.h"

typedef struct {
    uint32_t seq_id;
    int32_t  delta_ticks;
} RoutedPacket;

static int32_t settle_primary_path(RoutedPacket *pkt) {
    return pkt ? (pkt->delta_ticks + 10) : 0;
}

static int32_t settle_backup_path(RoutedPacket *pkt) {
    return pkt ? (pkt->delta_ticks - 5) : 0;
}

static int32_t shift_clamp_helper(int32_t acc, int32_t gate) {
    int32_t scaled = acc >> 1;
    return veriobjc_cmux(gate > 0, scaled, 0);
}

veriobjc_hw_kernel
int32_t zero_dsp_helper_pipeline(int32_t lane_a, int32_t lane_b, int32_t gate) {
    return shift_clamp_helper(lane_a + lane_b, gate);
}

int32_t route_packet_cfg_demo(struct veriobjc_stack *stack, int use_primary) {
    if (!stack) {
        return 0;
    }

    int32_t settled = 0;
    {
        VERIOBJC_SCOPED_STACK(stack);
        RoutedPacket *scratch = (RoutedPacket *)veriobjc_stack_alloc(stack, 2u * sizeof(RoutedPacket));
        if (!scratch) {
            return 0;
        }
        RoutedPacket *slot0 = &scratch[0];
        slot0->seq_id = 7u;
        slot0->delta_ticks = 40;

        if (use_primary) {
            settled = settle_primary_path(veriobjc_move(slot0));
        } else {
            settled = settle_backup_path(veriobjc_move(slot0));
        }
    }

    return zero_dsp_helper_pipeline(settled, 10, 1);
}

#ifdef VERIOBJC_EXAMPLE_MAIN
int main(void) {
    uint8_t arena_buf[128];
    struct veriobjc_stack st;
    veriobjc_stack_init(&st, arena_buf, sizeof(arena_buf));
    int32_t fast_val = route_packet_cfg_demo(&st, 1); // (40 + 10 + 10) >> 1 = 30
    int32_t slow_val = route_packet_cfg_demo(&st, 0); // (40 - 5 + 10) >> 1 = 22
    return (fast_val == 30 && slow_val == 22 && veriobjc_stack_used(&st) == 0u) ? 0 : 1;
}
#endif
