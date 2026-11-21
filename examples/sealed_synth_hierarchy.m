// Freestanding @sealed Synth Hierarchy Showcase (Task C2.3):
//   - Closed-world @sealed protocol + @sealed root class + mixed-size @final subclasses
//   - Scoped arena instantiation via veriobjc_alloc_sealed (zero heap, zero -lobjc)
//   - Direct @final leaf dispatch AND polymorphic tag-switched dispatch via AudioNode *
//   - Single-store VERIOBJC_SCOPED_STACK reset on scope exit
#include "veriobjc_runtime.h"
#include "rt/dispatch.h"

typedef struct {
    int32_t phase_q16;
    int32_t step_q16;
} SynthState;

__attribute__((objc_sealed))
@protocol AudioProcessorProtocol
- (int32_t)processNextSample:(SynthState *)state;
@end

__attribute__((objc_root_class))
__attribute__((objc_sealed))
@interface AudioNode <AudioProcessorProtocol> {
@public
    bool isActive;
    int32_t dc_offset_q16;
}
- (int32_t)processNextSample:(SynthState *)state;
@end

@implementation AudioNode
- (int32_t)processNextSample:(SynthState *)state {
    (void)state;
    return isActive ? dc_offset_q16 : 0;
}
@end

__attribute__((objc_sealed))
__attribute__((objc_final))
@interface SineOscillator : AudioNode {
@public
    int32_t amplitude_scale;
}
- (int32_t)processNextSample:(SynthState *)state;
@end

@implementation SineOscillator
- (int32_t)processNextSample:(SynthState *)state {
    if (!state || !isActive) {
        return 0;
    }
    int32_t sample = state->phase_q16 * amplitude_scale + dc_offset_q16;
    state->phase_q16 += state->step_q16;
    return sample;
}
@end

__attribute__((objc_sealed))
__attribute__((objc_final))
@interface SubBassOscillator : AudioNode {
@public
    int64_t sub_accum_q16;
    int32_t harmonic_shift;
}
- (int32_t)processNextSample:(SynthState *)state;
@end

@implementation SubBassOscillator
- (int32_t)processNextSample:(SynthState *)state {
    if (!state || !isActive) {
        return 0;
    }
    state->phase_q16 += (state->step_q16 >> 1);
    return (int32_t)(sub_accum_q16 >> harmonic_shift) + dc_offset_q16;
}
@end

int32_t render_sine_frame(struct veriobjc_stack *stack, SineOscillator *osc) {
    if (!stack || !osc) {
        return 0;
    }

    VERIOBJC_SCOPED_STACK(stack);
    SynthState *state = (SynthState *)veriobjc_stack_alloc(stack, sizeof(SynthState));
    if (!state) {
        return 0;
    }

    state->phase_q16 = 10;
    state->step_q16 = 2;

    // Monomorphic direct call to @_i_SineOscillator__processNextSample_
    return [osc processNextSample:state];
}

__attribute__((noinline))
int32_t render_polymorphic_voice(AudioNode *voice, SynthState *state) {
    if (!voice || !state) {
        return 0;
    }
    // Polymorphic closed-world switch via @__veriobjc_dispatch_AudioNode__processNextSample_
    return [voice processNextSample:state];
}

int main(void) {
    alignas(16) uint8_t arena[256];
    struct veriobjc_stack stack;
    veriobjc_stack_init(&stack, arena, sizeof(arena));

    {
        VERIOBJC_SCOPED_STACK(&stack);

        SineOscillator *sine = veriobjc_alloc_sealed(SineOscillator, &stack);
        SubBassOscillator *bass = veriobjc_alloc_sealed(SubBassOscillator, &stack);
        SynthState *state = (SynthState *)veriobjc_stack_alloc(&stack, sizeof(SynthState));
        if (!sine || !bass || !state) {
            return 1;
        }

        sine->isActive = true;
        sine->dc_offset_q16 = 2;
        sine->amplitude_scale = 4;

        bass->isActive = true;
        bass->dc_offset_q16 = 5;
        bass->sub_accum_q16 = 80;
        bass->harmonic_shift = 1;

        state->phase_q16 = 10;
        state->step_q16 = 2;

        // Direct @final send: 10 * 4 + 2 == 42 (phase becomes 12)
        if ([sine processNextSample:state] != 42) {
            return 2;
        }

        // Polymorphic @sealed send through AudioNode * base pointer: 12 * 4 + 2 == 50
        AudioNode *voice_a = sine;
        if (render_polymorphic_voice(voice_a, state) != 50) {
            return 3;
        }

        // Polymorphic @sealed send on mixed-size SubBassOscillator: (80 >> 1) + 5 == 45
        AudioNode *voice_b = bass;
        if (render_polymorphic_voice(voice_b, state) != 45) {
            return 4;
        }

        // Direct helper check: 10 * 4 + 2 == 42
        if (render_sine_frame(&stack, sine) != 42) {
            return 5;
        }
    }

    if (veriobjc_stack_used(&stack) != 0u) {
        return 6;
    }
    return 0;
}
