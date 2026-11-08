// Negative test: general heap allocation (malloc) is banned in @nolatency contexts
#include "veriobjc_runtime.h"

extern void *malloc(size_t size);

veriobjc_nolatency
void *allocate_in_hot_loop(size_t n) {
    return malloc(n);
}
