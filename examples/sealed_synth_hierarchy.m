#include "veriobjc_runtime.h"

//Compile-time constants
#define SAMPLE_RATE 44100u

typedef struct {
    float frequency;
    float phase;
} SynthState;

__attribute__((objc_sealed))
@protocol AudioProcessorProtocol
- (float)processNextSample:(SynthState *)state;
@end

__attribute__((objc_root_class))
__attribute__((objc_sealed))
@interface AudioNode <AudioProcessorProtocol> {
@public
    bool isActive;
}
- (float)processNextSample:(SynthState *)state;
@end

@implementation AudioNode
- (float)processNextSample:(SynthState *)state {
    (void)state;
    return 0.0f;
}
@end

__attribute__((objc_sealed))
__attribute__((objc_final))
@interface SineOscillator : AudioNode {
@public
    float amplitude;
}
- (float)processNextSample:(SynthState *)state;
@end

@implementation SineOscillator
- (float)processNextSample:(SynthState *)state {
    if (!state || !isActive) {
        return 0.0f;
    }
    float sample = state->phase * amplitude;
    state->phase += state->frequency / (float)SAMPLE_RATE;
    return sample;
}
@end

// Entrypoint showcasing sealed hierarchy + devirtualized direct call lowering
float render_sine_frame(struct veriobjc_stack *stack, SineOscillator *osc) {
    if (!stack || !osc) {
        return 0.0f;
    }

    VERIOBJC_SCOPED_STACK(stack);
    SynthState *state = (SynthState *)veriobjc_stack_alloc(stack, sizeof(SynthState));
    if (!state) {
        return 0.0f;
    }

    state->frequency = 440.0f;
    state->phase = 0.5f;

    // Lowered by VeriObjCPlugin to __veriobjc_direct_SineOscillator__processNextSample_
    return [osc processNextSample:state];
}
