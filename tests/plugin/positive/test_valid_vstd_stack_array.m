// Positive test: <vstd/stack.h> single growable vstack_array per data stack
#include <vstd/stack.h>

vstack_array_def(SampleArray, int32_t);

int32_t collect_samples(struct veriobjc_stack *st) {
    VERIOBJC_SCOPED_STACK(st);
    SampleArray samples;
    vstack_array_init(&samples, st);

    int32_t *first = vstack_array_push(&samples, 10);
    vstack_array_push(&samples, 20);
    return first ? *first : 0;
}
