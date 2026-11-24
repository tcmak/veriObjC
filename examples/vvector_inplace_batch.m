// examples/vvector_inplace_batch.m
// Demonstrates Task 3.3, Task 3.3b & Task C2.1b (<vstd/vector.h>):
//   - Constrained vgeneric @interface VVector<T : VVectorItem *> with devirtualized methods & range-for
//   - AST-checked VVector_decl / VVector(T, N) C11 macro containers with arbitrary capacities N (N = 4, N = 8)
//   - Built-in-place slot construction via vvector_push_inplace
#include "veriobjc_runtime.h"
#include "rt/dispatch.h"
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

VVector_decl(int32_t, 4);
VVector_decl(OrderBatchVec8, OrderBatchItem *, 8);

static void construct_order_lot_inplace(int32_t *out_slot, int32_t base_qty) {
    if (out_slot) {
        *out_slot = base_qty * 100;
    }
}

OrderBatchVec8 build_order_batch_by_value(
    OrderBatchItem *item0,
    OrderBatchItem *item1
) {
    OrderBatchVec8 out;
    vvector_init(&out);
    vvector_push(&out, item0);
    vvector_push(&out, item1);
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

typedef VVector<OrderBatchItem *> OrderBatchObjVec;

int32_t run_vvector_batch_demo(void) {
    _Alignas(8) uint8_t arena_backing[256];
    struct veriobjc_stack arena;
    veriobjc_stack_init(&arena, arena_backing, sizeof(arena_backing));
    VERIOBJC_SCOPED_STACK(&arena);

    OrderBatchItem *item0 = veriobjc_alloc_sealed(OrderBatchItem, &arena);
    OrderBatchItem *item1 = veriobjc_alloc_sealed(OrderBatchItem, &arena);
    OrderBatchObjVec *obj_vec = veriobjc_alloc_sealed(OrderBatchObjVec, &arena);
    if (!item0 || !item1 || !obj_vec) {
        return 0;
    }
    item0->item_tag = 1u;
    item0->qty      = 25;
    item1->item_tag = 2u;
    item1->qty      = 25;

    OrderBatchVec8 by_val_vec = build_order_batch_by_value(item0, item1);
    OrderBatchItem **first_slot = vvector_at(&by_val_vec, 0u);
    OrderBatchItem **second_slot = vvector_at(&by_val_vec, 1u);
    if (!first_slot || !second_slot || vvector_cap(&by_val_vec) != 8u) {
        return 0;
    }

    int32_t generic_sum = evaluate_generic_order_batch(
        obj_vec,
        *first_slot,
        *second_slot
    );

    VVector(int32_t, 4) batch;
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
