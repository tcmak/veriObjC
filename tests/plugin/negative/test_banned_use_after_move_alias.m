// Negative test: reading a linear object through a local alias after the root variable was moved
#include "veriobjc_runtime.h"

typedef struct {
    uint32_t order_id;
    int32_t  qty;
} AliasedOrder;

static void sink_aliased_order(AliasedOrder *ord) {
    (void)ord;
}

int32_t use_after_move_via_alias(AliasedOrder *incoming) {
    if (!incoming) return 0;
    AliasedOrder *pkt = incoming;
    AliasedOrder *alias = pkt;
    sink_aliased_order(veriobjc_move(pkt));
    // Violation: 'alias' aliases consumed root 'pkt'
    return alias->qty;
}
