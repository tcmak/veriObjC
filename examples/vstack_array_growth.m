// ===========================================================================
// VeriObjC Phase 3 (Task 3.2): <vstd/stack.h>
// Grow-only data-stack arrays with stable references & 1-store scope reset
// ===========================================================================

#include <vstd/stack.h>

vstack_array_def(TickSeriesArray, int64_t);

int64_t accumulate_scoped_ticks(struct veriobjc_stack *data_stack) {
    VERIOBJC_SCOPED_STACK(data_stack);

    TickSeriesArray ticks;
    vstack_array_init(&ticks, data_stack);

    int64_t *anchor = vstack_array_push(&ticks, 100);
    for (int64_t i = 1; i <= 16; ++i) {
        vstack_array_push(&ticks, i * 10);
    }

    if (anchor) {
        *anchor += 50;
        return *anchor + (int64_t)ticks.count;
    }
    return 0;
}
