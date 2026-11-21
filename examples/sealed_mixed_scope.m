// Mixed-Size @sealed Hierarchy + Scoped Arena + vspan Composition Showcase (Task 5.5):
//   - Three concrete @sealed @final subclasses with differing ivar counts & byte sizes (8B, 16B, 32B)
//   - Allocated in a single VERIOBJC_SCOPED_STACK on one veriobjc_stack
//   - Stored in a <vstd/span.h> slice of superclass-typed pointers (PacketStageNode *)
//   - Polymorphic tag-switched dispatch across the heterogeneous batch + one-store scope reset
#include "veriobjc_runtime.h"
#include "rt/dispatch.h"
#include "vstd/span.h"

__attribute__((objc_root_class))
__attribute__((objc_sealed))
@interface PacketStageNode {
@public
    int32_t stage_code;
}
- (int32_t)applyToPacketLen:(int32_t)pkt_len;
@end

@implementation PacketStageNode
- (int32_t)applyToPacketLen:(int32_t)pkt_len {
    return pkt_len + stage_code;
}
@end

// 8-byte concrete subclass
__attribute__((objc_sealed))
__attribute__((objc_final))
@interface HeaderTrimStage : PacketStageNode {
@public
    uint16_t strip_bytes;
    uint16_t min_floor;
}
- (int32_t)applyToPacketLen:(int32_t)pkt_len;
@end

@implementation HeaderTrimStage
- (int32_t)applyToPacketLen:(int32_t)pkt_len {
    int32_t trimmed = pkt_len - (int32_t)strip_bytes;
    return (trimmed > (int32_t)min_floor) ? trimmed : (int32_t)min_floor;
}
@end

// 16-byte concrete subclass
__attribute__((objc_sealed))
__attribute__((objc_final))
@interface TrailerPadStage : PacketStageNode {
@public
    int32_t trailer_crc_bytes;
    int32_t align_mask;
    int32_t tag_overhead;
}
- (int32_t)applyToPacketLen:(int32_t)pkt_len;
@end

@implementation TrailerPadStage
- (int32_t)applyToPacketLen:(int32_t)pkt_len {
    int32_t raw = pkt_len + trailer_crc_bytes + tag_overhead;
    return (raw + align_mask) & ~align_mask;
}
@end

// 32-byte concrete subclass
__attribute__((objc_sealed))
__attribute__((objc_final))
@interface WideTunnelEncapStage : PacketStageNode {
@public
    int64_t tunnel_vni;
    int64_t sequence_epoch;
    int32_t outer_ip_bytes;
    int32_t udp_hdr_bytes;
}
- (int32_t)applyToPacketLen:(int32_t)pkt_len;
@end

@implementation WideTunnelEncapStage
- (int32_t)applyToPacketLen:(int32_t)pkt_len {
    int32_t vni_bias = (int32_t)((tunnel_vni + sequence_epoch) & 0xFF);
    return pkt_len + outer_ip_bytes + udp_hdr_bytes + vni_bias;
}
@end

vspan_def(PacketStageSpan, PacketStageNode *);

__attribute__((noinline))
int32_t execute_stage_pipeline(PacketStageSpan stages, int32_t initial_len) {
    int32_t cur_len = initial_len;
    for (size_t i = 0u; i < stages.len; ++i) {
        PacketStageNode **slot = vspan_at(&stages, i);
        if (slot && *slot) {
            cur_len = [*slot applyToPacketLen:cur_len];
        }
    }
    return cur_len;
}

int main(void) {
    alignas(16) uint8_t arena_buf[384];
    struct veriobjc_stack stack;
    veriobjc_stack_init(&stack, arena_buf, sizeof(arena_buf));

    int32_t final_wire_len = 0;
    {
        VERIOBJC_SCOPED_STACK(&stack);

        HeaderTrimStage      *s0 = veriobjc_alloc_sealed(HeaderTrimStage, &stack);
        WideTunnelEncapStage *s1 = veriobjc_alloc_sealed(WideTunnelEncapStage, &stack);
        TrailerPadStage      *s2 = veriobjc_alloc_sealed(TrailerPadStage, &stack);
        if (!s0 || !s1 || !s2) {
            return 1;
        }

        // 100 - 14 = 86
        s0->stage_code  = 1;
        s0->strip_bytes = 14u;
        s0->min_floor   = 64u;

        // 86 + 20 + 8 + ((3 + 1) & 0xFF) = 118
        s1->stage_code     = 2;
        s1->tunnel_vni     = 3;
        s1->sequence_epoch = 1;
        s1->outer_ip_bytes = 20;
        s1->udp_hdr_bytes  = 8;

        // (118 + 4 + 2 + 7) & ~7 = 131 & ~7 = 128
        s2->stage_code        = 3;
        s2->trailer_crc_bytes = 4;
        s2->align_mask        = 7;
        s2->tag_overhead      = 2;

        PacketStageNode *pipeline_nodes[3] = { s0, s1, s2 };
        PacketStageSpan pipeline_span = vspan_from_array(PacketStageSpan, pipeline_nodes);
        final_wire_len = execute_stage_pipeline(pipeline_span, 100);
    }

    if (final_wire_len != 128) {
        return 2;
    }
    if (veriobjc_stack_used(&stack) != 0u) {
        return 3;
    }
    return 0;
}
