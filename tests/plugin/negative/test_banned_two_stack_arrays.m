// Negative test: two live growable vstack_array instances on the same data stack
#include <vstd/stack.h>

vstack_array_def(LevelArray, uint32_t);

void two_growable_arrays_on_one_stack(struct veriobjc_stack *st) {
    LevelArray bids;
    LevelArray asks;
    vstack_array_init(&bids, st);
    vstack_array_init(&asks, st);
}
