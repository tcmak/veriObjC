// Valid stock Objective-C: open-world subclassing without __attribute__((objc_sealed)).
// Rejected by VeriObjC because polymorphic inheritance hierarchies must be @sealed
// so closed-world 16-bit tag-switched dispatch tables can be statically synthesized.
#include <stdint.h>

__attribute__((objc_root_class))
@interface UnsealedSynthBase {
@public
    int32_t base_gain;
}
- (int32_t)computeSample;
@end

@implementation UnsealedSynthBase
- (int32_t)computeSample {
    return base_gain;
}
@end

@interface OpenWorldLeadVoice : UnsealedSynthBase {
@public
    int32_t detune_cents;
}
- (int32_t)computeSample;
@end

@implementation OpenWorldLeadVoice
- (int32_t)computeSample {
    return base_gain + detune_cents;
}
@end
