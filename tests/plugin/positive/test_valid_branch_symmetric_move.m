// Positive test: moving a linear value symmetrically in both 'then' and 'else' branches with no use after join
#include "veriobjc_runtime.h"

typedef struct {
    uint32_t seq;
    int32_t  price_ticks;
} BranchOrder;

static int32_t route_fast_lane(BranchOrder *ord) {
    return ord ? ord->price_ticks : 0;
}

static int32_t route_recovery_lane(BranchOrder *ord) {
    return ord ? -ord->price_ticks : 0;
}

int32_t dispatch_branch_symmetric_move(BranchOrder *incoming, int use_fast) {
    BranchOrder *pkt = incoming;
    int32_t result = 0;
    if (use_fast) {
        result = route_fast_lane(veriobjc_move(pkt));
    } else {
        result = route_recovery_lane(veriobjc_move(pkt));
    }
    // No use of 'pkt' after the if/else join block — valid!
    return result;
}
