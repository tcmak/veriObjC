// Valid stock Objective-C/C: reading through a pointer to a struct field after a
// value copy wrapped in veriobjc_move.
// Rejected by VeriObjC (Task 6.0.8): aliases rooted in a moved struct (&pkt.qty)
// are invalidated when the owning struct is consumed.
#include "veriobjc_runtime.h"

typedef struct {
    uint32_t id_num;
    int32_t  qty;
} CorpusOrderPacket;

static void forward_packet(CorpusOrderPacket pkt) {
    (void)pkt;
}

int32_t valid_objc_read_field_alias_after_move(void) {
    CorpusOrderPacket pkt = { .id_num = 7u, .qty = 25 };
    int32_t *qty_ptr = &pkt.qty;
    forward_packet(veriobjc_move(pkt));
    return *qty_ptr;
}
