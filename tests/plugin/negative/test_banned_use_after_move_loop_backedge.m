// Negative test: moving an outer linear variable inside a loop body causes use-after-move across the loop back-edge
#include "veriobjc_runtime.h"

typedef struct {
    uint32_t batch_id;
} LoopLinearBatch;

static void consume_loop_batch(LoopLinearBatch *b) {
    (void)b;
}

void use_after_move_across_loop_backedge(LoopLinearBatch *initial, int count) {
    LoopLinearBatch *batch = initial;
    for (int i = 0; i < count; ++i) {
        consume_loop_batch(veriobjc_move(batch));
    }
}
