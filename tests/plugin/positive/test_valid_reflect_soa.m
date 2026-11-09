// Positive test: Compile-time @reflect() intrinsics and __attribute__((objc_soa)) layout lowering
#include "veriobjc_runtime.h"

struct NetworkPacket {
    uint64_t timestamp_ns;
    uint32_t seq_no;
    uint16_t payload_len;
    uint16_t flags;
};

vsoa_def3(ParticleSoA, 256, float, x, float, y, uint32_t, id);

uint32_t verify_reflection_and_soa(struct ParticleSoA *soa) {
    uint32_t fields = veriobjc_reflect_field_count(struct NetworkPacket);
    uint32_t pkt_size = veriobjc_reflect_sizeof(struct NetworkPacket);
    uint32_t seq_off = (uint32_t)veriobjc_reflect_offset(struct NetworkPacket, seq_no);

    if (soa) {
        soa->x[0] = 1.0f;
        soa->y[0] = 2.0f;
        soa->id[0] = fields + pkt_size + seq_off;
        return soa->id[0];
    }
    return fields + pkt_size + seq_off;
}
