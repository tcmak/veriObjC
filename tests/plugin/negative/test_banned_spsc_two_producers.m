// EXPECTED_DIAGNOSTIC: VeriObjC Violation: SPSC invariant violated on channel
#include "veriobjc_runtime.h"
#include "vstd/channel.h"

vspsc_channel_def(SharedSPSC4, int32_t, 4);
static SharedSPSC4 g_chan;

void producer_alpha(void) {
    vspsc_channel_send(&g_chan, 1);
}

void producer_beta(void) {
    // Second distinct producer function writing to the same SPSC channel is banned
    vspsc_channel_send(&g_chan, 2);
}
