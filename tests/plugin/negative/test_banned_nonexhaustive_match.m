// Negative test: non-exhaustive vmatch over a vvariant must fail compilation
#include "veriobjc_runtime.h"

enum OrderStateTag {
    OrderState_New = 0,
    OrderState_Filled = 1,
    OrderState_Cancelled = 2
};

struct veriobjc_vvariant OrderStateVariant {
    enum OrderStateTag tag;
    uint32_t qty;
};

uint32_t audit_order_state(struct OrderStateVariant state) {
    uint32_t total = 0;
    vmatch (state) {
        case OrderState_New:
            total = 1;
            break;
        case OrderState_Filled:
            total = state.qty;
            break;
        // Missing OrderState_Cancelled!
    }
    return total;
}
