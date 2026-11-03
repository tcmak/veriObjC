#include "veriobjc_runtime.h"

// Flat, contiguous packet structure (Zero Base Tax)
typedef struct {
    uint32_t stream_id;
    uint32_t payload_len;
    float    gain;
} AudioPacket;

// Freestanding Objective-C root class (No NSObject inheritance)
__attribute__((objc_root_class))
@interface AudioSynthesizer
- (void)processPacket:(AudioPacket *)packet;
- (float)calculatePeakGain:(const AudioPacket *)packet;
@end

@implementation AudioSynthesizer
- (void)processPacket:(AudioPacket *)packet {
    if (!packet) {
        return;
    }
    packet->gain = packet->gain * 0.95f;
}

- (float)calculatePeakGain:(const AudioPacket *)packet {
    if (!packet) {
        return 0.0f;
    }
    return packet->gain;
}
@end

// Example entry function showcasing scoped data stacks and nominal messaging
void run_audio_pipeline(struct veriobjc_stack *stack, AudioSynthesizer *synth) {
    if (!stack || !synth) {
        return;
    }

    // Demonstrates @scoped data-stack RAII:
    // Memory allocated inside this block automatically resets with a single
    // store instruction when the scope exits.
    VERIOBJC_SCOPED_STACK(stack);

    AudioPacket *pkt = (AudioPacket *)veriobjc_stack_alloc(stack, sizeof(AudioPacket));
    if (!pkt) {
        return;
    }

    pkt->stream_id = 101;
    pkt->payload_len = 512;
    pkt->gain = 1.0f;

    // Nominally-typed message dispatch verified by VeriObjCPlugin
    [synth processPacket:pkt];
}
