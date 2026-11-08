// Positive test: constrained generic class `<T : SealedConstraint *>` checked and devirtualized at definition site
#include "veriobjc_runtime.h"

__attribute__((objc_root_class))
__attribute__((objc_sealed))
@interface InstrumentQuote {
@public
    int64_t bid_ticks;
    int64_t ask_ticks;
}
- (int64_t)spreadTicks;
@end

@implementation InstrumentQuote
- (int64_t)spreadTicks {
    return ask_ticks - bid_ticks;
}
@end

__attribute__((objc_sealed))
__attribute__((objc_final))
@interface EquityQuote : InstrumentQuote {
@public
    uint32_t venue_code;
}
@end

@implementation EquityQuote
@end

__attribute__((objc_root_class))
__attribute__((objc_sealed))
__attribute__((objc_final))
@interface QuoteVault<T : InstrumentQuote *> {
@public
    int32_t vault_id;
}
- (int64_t)evaluateSpread:(T)quote;
@end

@implementation QuoteVault
- (int64_t)evaluateSpread:(InstrumentQuote *)quote {
    if (!quote) {
        return 0;
    }
    return [quote spreadTicks];
}
@end

int64_t query_equity_vault(QuoteVault<EquityQuote *> *vault, EquityQuote *eq) {
    if (!vault || !eq) {
        return 0;
    }
    return [vault evaluateSpread:eq];
}
