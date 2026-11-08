// Negative test: reading or messaging a linear variable after veriobjc_move() is banned
#include "veriobjc_runtime.h"

typedef struct {
    uint32_t seq;
    uint32_t qty;
} OrderPacket;

void consume_order(OrderPacket *pkt) {
    (void)pkt;
}

void test_use_after_move(OrderPacket *incoming) {
    OrderPacket *local_pkt = incoming;
    consume_order(veriobjc_move(local_pkt));

    // Violation: local_pkt was moved on the previous line!
    local_pkt->qty = 0;
}
