// Positive test: Exhaustive vmatch on vvariant and vtrait closed compile-time table evaluation
#include "veriobjc_runtime.h"

enum VenueOrderTag {
    VenueOrder_New = 0,
    VenueOrder_Modified = 1,
    VenueOrder_Cancelled = 2
};

struct veriobjc_vvariant VenueOrderEvent {
    enum VenueOrderTag tag;
    uint32_t qty;
    uint32_t reason;
};

vtrait uint32_t hash_strategy_kind(uint32_t type_id) {
    if (type_id == 1u) {
        return 10u; // IdentityHash
    }
    if (type_id == 2u) {
        return 20u; // FibonacciMix
    }
    return 0u;
}

uint32_t dispatch_venue_event(struct VenueOrderEvent ev) {
    uint32_t trait_kind = hash_strategy_kind(1u);
    uint32_t out = 0u;
    vmatch (ev) {
        case VenueOrder_New:
            out = ev.qty + trait_kind;
            break;
        case VenueOrder_Modified:
            out = ev.qty;
            break;
        case VenueOrder_Cancelled:
            out = ev.reason;
            break;
    }
    return out;
}
