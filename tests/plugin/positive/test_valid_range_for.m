// Positive test: range-based for-in loop over a statically bounded container lowered to raw pointer ForStmt
#include "veriobjc_runtime.h"

__attribute__((objc_root_class))
__attribute__((objc_sealed))
__attribute__((objc_final))
@interface AudioFrame {
@public
    int32_t sample;
}
- (int32_t)readSample;
@end

@implementation AudioFrame
- (int32_t)readSample {
    return sample;
}
@end

__attribute__((objc_root_class))
__attribute__((objc_sealed))
__attribute__((objc_final))
@interface FixedFrameBatch {
@public
    AudioFrame *frames[8];
}
@end

@implementation FixedFrameBatch
@end

int32_t accumulate_frames(FixedFrameBatch *batch) {
    int32_t acc = 0;
    for (AudioFrame *f in batch) {
        if (f) {
            acc += [f readSample];
        }
    }
    return acc;
}
