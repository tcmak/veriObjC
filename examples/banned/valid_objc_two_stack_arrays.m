// Valid stock Objective-C/C: initializing two growable arrays on the same bump allocator.
// Rejected by VeriObjC (Task 3.2): at most one live growable vstack_array per data stack.
#include <vstd/stack.h>

vstack_array_def(OrderSliceArray, uint32_t);

void valid_objc_two_arrays_same_stack(struct veriobjc_stack *st) {
    OrderSliceArray first_arr;
    OrderSliceArray second_arr;
    vstack_array_init(&first_arr, st);
    vstack_array_init(&second_arr, st);
}
