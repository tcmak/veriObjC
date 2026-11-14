// examples/vvector_inplace_batch.m
// Demonstrates Task 3.3 (<vstd/vector.h>):
//   - Fixed-capacity inplace container vvector<T, N> with zero heap allocation
//   - Built-in-place slot construction via vvector_push_inplace
#include "veriobjc_runtime.h"
#include "vstd/vector.h"

vvector_def(OrderBatchVec4, int32_t, 4);

static void construct_order_lot_inplace(int32_t *out_slot, int32_t base_qty) {
    if (out_slot) {
        *out_slot = base_qty * 100;
    }
}

int32_t run_vvector_batch_demo(void) {
    OrderBatchVec4 batch;
    vvector_init(&batch);
    vvector_push(&batch, 50);
    vvector_push_inplace(&batch, construct_order_lot_inplace, 3);
    int32_t *a = vvector_at(&batch, 0u);
    int32_t *b = vvector_at(&batch, 1u);
    return (a && b) ? (*a + *b) : 0;
}

#ifdef VERIOBJC_EXAMPLE_MAIN
int main(void) {
    return (run_vvector_batch_demo() == 350) ? 0 : 1;
}
#endif
