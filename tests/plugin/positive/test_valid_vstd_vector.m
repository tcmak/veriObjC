#include "veriobjc_runtime.h"
#include "vstd/vector.h"
#include "vstd/stack.h"

__attribute__((objc_sealed))
__attribute__((objc_final))
@interface OrderLotItem : VVectorItem {
@public
    int32_t lot_qty;
}
@end

@implementation OrderLotItem
@end

vvector_def(BatchVec4, int32_t, 4);
vstack_array_def(StackIntArr, int32_t);

static int32_t parse_lot_value(int32_t raw) {
    return raw * 10;
}

static void parse_into_slot(int32_t *out_slot, int32_t raw) {
    if (out_slot) {
        *out_slot = raw + 100;
    }
}

BatchVec4 build_vector_by_value(int32_t seed) {
    BatchVec4 batch;
    vvector_init(&batch);
    vvector_push(&batch, parse_lot_value(seed));
    vvector_push_inplace(&batch, parse_into_slot, seed);
    return batch;
}

StackIntArr build_stack_array_by_value(struct veriobjc_stack *s, int32_t a, int32_t b) {
    StackIntArr arr;
    vstack_array_init(&arr, s);
    vstack_array_push(&arr, a);
    vstack_array_push(&arr, b);
    return arr;
}

int32_t run_valid_vvector(VVector<OrderLotItem *> *vec, OrderLotItem *a, OrderLotItem *b) {
    if (!vec || !a || !b) {
        return 0;
    }
    [vec initVec];
    [vec push:a];
    [vec push:b];

    // VG-001: specialized return type OrderLotItem * from vgeneric -at:
    OrderLotItem *first = [vec at:0u];
    int32_t acc = first ? first->lot_qty : 0;

    // VG-002: range-based for loop over specialized VVector<OrderLotItem *>
    for (OrderLotItem *item in vec) {
        if (item) {
            acc += item->lot_qty;
        }
    }

    BatchVec4 by_val = build_vector_by_value(3);
    int32_t *p = vvector_at(&by_val, 0u);
    return acc + (p ? *p : 0);
}
