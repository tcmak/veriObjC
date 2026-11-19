// examples/vvector_inplace_batch.m
// Demonstrates Task 3.3, Task 3.3b & Task 3.3c (<vstd/vector.h>):
//   - Constrained vgeneric @interface VVector<T : VVectorItem *> with devirtualized methods & range-for
//   - Single unified container vvector<T, N> (VG-005) and flat VVector<T> by-value return (VG-006)
//   - Built-in-place slot construction via vvector_push_inplace
#include "veriobjc_runtime.h"
#include "vstd/vector.h"

__attribute__((annotate("objc_sealed")))
__attribute__((annotate("objc_final")))
@interface OrderBatchItem : VVectorItem {
@public
    int32_t qty;
}
@end

@implementation OrderBatchItem
@end

static void construct_order_lot_inplace(int32_t *out_slot, int32_t base_qty) {
    if (out_slot) {
        *out_slot = base_qty * 100;
    }
}

VVector<OrderBatchItem *> build_order_batch_by_value(
    OrderBatchItem *item0,
    OrderBatchItem *item1
) {
    VVector<OrderBatchItem *> out;
    [&out initVec];
    [&out push:item0];
    [&out push:item1];
    return out;
}

int32_t evaluate_generic_order_batch(
    VVector<OrderBatchItem *> *obj_vec,
    OrderBatchItem *item0,
    OrderBatchItem *item1
) {
    [obj_vec initVec];
    [obj_vec push:item0];
    [obj_vec push:item1];

    int32_t generic_sum = 0;
    for (OrderBatchItem *it in obj_vec) {
        if (it) {
            generic_sum += it->qty;
        }
    }
    return generic_sum;
}

int32_t run_vvector_batch_demo(void) {
    struct {
        uint32_t item_tag;
        int32_t qty;
    } raw0 = {1u, 25}, raw1 = {2u, 25};

    VVector<OrderBatchItem *> by_val_vec = build_order_batch_by_value(
        (OrderBatchItem *)&raw0,
        (OrderBatchItem *)&raw1
    );

    int32_t generic_sum = evaluate_generic_order_batch(
        (VVector<OrderBatchItem *> *)&by_val_vec,
        (OrderBatchItem *)&raw0,
        (OrderBatchItem *)&raw1
    );

    vvector<int32_t, 4> batch;
    vvector_init(&batch);
    vvector_push(&batch, generic_sum);
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
