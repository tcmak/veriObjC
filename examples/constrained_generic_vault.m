#include "veriobjc_runtime.h"

__attribute__((objc_root_class))
__attribute__((objc_sealed))
@interface MarketInstrument {
@public
    uint32_t instrument_id;
    int64_t  reference_price_ticks;
}
- (int64_t)referencePriceTicks;
@end

@implementation MarketInstrument
- (int64_t)referencePriceTicks {
    return reference_price_ticks;
}
@end

__attribute__((objc_sealed))
__attribute__((objc_final))
@interface FuturesContract : MarketInstrument {
@public
    uint32_t expiry_yyyymm;
}
@end

@implementation FuturesContract
@end

__attribute__((objc_root_class))
__attribute__((objc_sealed))
__attribute__((objc_final))
@interface InstrumentVault<T : MarketInstrument *> {
@public
    uint32_t vault_slot_id;
}
- (int64_t)computeNotionalTicks:(T)inst quantity:(int64_t)qty;
@end

@implementation InstrumentVault
- (int64_t)computeNotionalTicks:(MarketInstrument *)inst quantity:(int64_t)qty {
    if (!inst) {
        return 0;
    }
    return [inst referencePriceTicks] * qty;
}
@end

int64_t evaluate_futures_notional(InstrumentVault<FuturesContract *> *vault, FuturesContract *fut, int64_t qty) {
    if (!vault || !fut) {
        return 0;
    }
    return [vault computeNotionalTicks:fut quantity:qty];
}
