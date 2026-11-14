#include "veriobjc_runtime.h"
#include "vstd/channel.h"
#include "vstd/atomic.h"

vspsc_channel_def(QuoteChan4, int32_t, 4);
vatomic_def(SeqAtomic, int32_t);

int32_t run_valid_channel_and_atomic(void) {
    QuoteChan4 qchan;
    vspsc_channel_init(&qchan);
    vspsc_channel_send(&qchan, 42);

    int32_t msg = 0;
    vspsc_channel_recv(&qchan, &msg);

    SeqAtomic seq;
    vatomic_init(&seq, 0);
    {
        vatomic_acquire(guard, &seq);
        int32_t *ptr = vatomic_get(&guard);
        if (ptr) {
            *ptr += msg;
        }
    }
    return msg;
}
