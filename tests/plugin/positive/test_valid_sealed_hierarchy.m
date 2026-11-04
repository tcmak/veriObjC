// Positive test: sealed class hierarchy with ivar access and devirtualized method dispatch
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float frequency;
    float phase;
} SynthState;

__attribute__((objc_root_class))
__attribute__((objc_sealed))
@interface AudioNode {
@public
    bool isActive;
}
- (float)processNextSample:(SynthState *)state;
@end

__attribute__((objc_sealed))
@interface SineOscillator : AudioNode {
@public
    float amplitude;
}
- (float)processNextSample:(SynthState *)state;
@end

@implementation SineOscillator
- (float)processNextSample:(SynthState *)state {
    float sample = state->phase * amplitude;
    state->phase += state->frequency / 44100.0f;
    return sample;
}
@end

float run_oscillator(SineOscillator *osc, SynthState *state) {
    if (!osc || !state) {
        return 0.0f;
    }
    return [osc processNextSample:state];
}
