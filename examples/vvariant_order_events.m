// examples/vvariant_order_events.m
// Demonstrates Task 3.5 (<vstd/variant.h>):
//   - Closed algebraic sum type vvariant with zero-overhead inline union storage
//   - Exhaustive jump-table dispatch via vmatch verified by VeriObjCPlugin
#include "veriobjc_runtime.h"
#include "vstd/variant.h"

typedef enum : uint32_t {
    EVT_ACK = 0,
    EVT_FILL = 1,
    EVT_REJECT = 2
} OrderEventKind;

vvariant_def(OrderEvent, OrderEventKind, {
    int32_t ack_id;
    int32_t fill_shares;
    int32_t reject_code;
});

int32_t eval_order_event(OrderEvent ev) {
    vmatch(ev) {
        case EVT_ACK:
            return ev.payload.ack_id;
        case EVT_FILL:
            return ev.payload.fill_shares * 2;
        case EVT_REJECT:
            return -ev.payload.reject_code;
    }
    return 0;
}

#ifdef VERIOBJC_EXAMPLE_MAIN
int main(void) {
    OrderEvent fill_ev = vvariant_make(OrderEvent, EVT_FILL, fill_shares, 21);
    return (eval_order_event(fill_ev) == 42) ? 0 : 1;
}
#endif
