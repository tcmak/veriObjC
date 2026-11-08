#include "veriobjc_runtime.h"

__attribute__((objc_root_class))
__attribute__((objc_sealed))
__attribute__((objc_final))
@interface SignalTap {
@public
    int32_t coefficient;
    int32_t sample_history;
}
- (int32_t)evaluateTap;
@end

@implementation SignalTap
- (int32_t)evaluateTap {
    return coefficient * sample_history;
}
@end

__attribute__((objc_root_class))
__attribute__((objc_sealed))
__attribute__((objc_final))
@interface FirFilterBank {
@public
    SignalTap *taps[16];
}
- (int32_t)computeFilterOutput;
@end

@implementation FirFilterBank
- (int32_t)computeFilterOutput {
    int32_t sum = 0;
    for (SignalTap *tap in self) {
        if (tap) {
            sum += [tap evaluateTap];
        }
    }
    return sum;
}
@end

int32_t run_fir_filter_bank(FirFilterBank *bank) {
    if (!bank) {
        return 0;
    }
    return [bank computeFilterOutput];
}
