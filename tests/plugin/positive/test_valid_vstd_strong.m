// Positive test: <vstd/strong.h> newtype wrappers preserve nominal identity under auto
#include <vstd/strong.h>

vstrong_def(VenuePrice, int64_t);
vstrong_def(VenueQty, int32_t);

veriobjc_pure
int64_t compute_notional_ticks(void) {
    auto px = vstrong_make(VenuePrice, 5000);
    auto qty = vstrong_make(VenueQty, 10);
    if (vstrong_lt(px, vstrong_make(VenuePrice, 10000))) {
        return vstrong_unwrap(px) * (int64_t)vstrong_unwrap(qty);
    }
    return 0;
}
