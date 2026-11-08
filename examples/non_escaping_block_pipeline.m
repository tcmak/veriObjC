#include "veriobjc_runtime.h"

__attribute__((objc_root_class))
__attribute__((objc_sealed))
__attribute__((objc_final))
@interface TickStreamProcessor {
@public
    int32_t threshold;
}
- (int32_t)filterAndAccumulate:(const int32_t *)ticks count:(uint32_t)n;
@end

@implementation TickStreamProcessor
- (int32_t)filterAndAccumulate:(const int32_t *)ticks count:(uint32_t)n {
    if (!ticks) {
        return 0;
    }

    int32_t min_tick = threshold;
    int32_t (^score_tick)(int32_t) = ^int32_t(int32_t t) {
        return (t > min_tick) ? (t - min_tick) : 0;
    };

    int32_t total = 0;
    for (uint32_t i = 0; i < n; ++i) {
        total += score_tick(ticks[i]);
    }
    return total;
}
@end

int32_t run_block_tick_pipeline(TickStreamProcessor *proc, const int32_t *ticks, uint32_t n) {
    if (!proc || !ticks) {
        return 0;
    }
    return [proc filterAndAccumulate:ticks count:n];
}
