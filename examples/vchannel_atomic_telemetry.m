// examples/vchannel_atomic_telemetry.m
// Demonstrates Task 3.8 (<vstd/channel.h>, <vstd/atomic.h>):
//   - Lock-free SPSC flat-value ring channel (vspsc_channel_def)
//   - Scoped linear atomic guard (vatomic_def / vatomic_acquire) with automatic release
#include "veriobjc_runtime.h"
#include "vstd/channel.h"
#include "vstd/atomic.h"

vspsc_channel_def(TelemetryRing4, int32_t, 4);
vatomic_def(AtomicLatencyAccumulator, int32_t);

int32_t run_channel_atomic_demo(void) {
    TelemetryRing4 ring;
    vspsc_channel_init(&ring);
    vspsc_channel_send(&ring, 15);
    vspsc_channel_send(&ring, 27);

    AtomicLatencyAccumulator total;
    vatomic_init(&total, 0);

    int32_t sample = 0;
    while (vspsc_channel_recv(&ring, &sample)) {
        vatomic_acquire(guard, &total);
        int32_t *val = vatomic_get(&guard);
        if (val) {
            *val += sample;
        }
    }

    int32_t final_val = 0;
    {
        vatomic_acquire(read_guard, &total);
        int32_t *val = vatomic_get(&read_guard);
        if (val) {
            final_val = *val;
        }
    }
    return final_val;
}

#ifdef VERIOBJC_EXAMPLE_MAIN
int main(void) {
    return (run_channel_atomic_demo() == 42) ? 0 : 1;
}
#endif
