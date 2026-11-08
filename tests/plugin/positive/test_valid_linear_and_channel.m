// Positive test: valid linear move semantics, non-escaping scoped data-stack refs, and flat channel payloads
#include "veriobjc_runtime.h"

__attribute__((objc_flat))
typedef struct {
    uint64_t order_id;
    int64_t  price_ticks;
    uint32_t quantity;
    uint32_t venue_id;
} FlatOrderMsg;

__attribute__((objc_root_class))
__attribute__((objc_sealed))
__attribute__((objc_final))
@interface OrderGateway {
@public
    uint32_t gateway_id;
}
- (bool)routeOrder:(FlatOrderMsg)msg intoChannel:(struct veriobjc_channel *)ch;
@end

@implementation OrderGateway
- (bool)routeOrder:(FlatOrderMsg)msg intoChannel:(struct veriobjc_channel *)ch {
    return veriobjc_channel_send(ch, &msg, sizeof(FlatOrderMsg));
}
@end

bool process_valid_linear_flow(struct veriobjc_stack *stack, OrderGateway *gw, struct veriobjc_channel *ch) {
    if (!stack || !gw || !ch) {
        return false;
    }

    VERIOBJC_SCOPED_STACK(stack);
    FlatOrderMsg *scratch = (FlatOrderMsg *)veriobjc_stack_alloc(stack, sizeof(FlatOrderMsg));
    if (!scratch) {
        return false;
    }
    scratch->order_id    = 1001u;
    scratch->price_ticks = 50025;
    scratch->quantity    = 100u;
    scratch->venue_id    = 1u;

    FlatOrderMsg local_msg = *scratch;
    // Move local_msg into the gateway call; never used afterwards
    return [gw routeOrder:veriobjc_move(local_msg) intoChannel:ch];
}
