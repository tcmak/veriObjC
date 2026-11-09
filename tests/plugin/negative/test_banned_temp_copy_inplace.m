// Negative test: intermediate stack copy of a built-in-place type is banned
#include "veriobjc_runtime.h"

struct veriobjc_inplace_only LargeOrderBookFrame {
    uint64_t seq;
    uint64_t levels[64];
};

void copy_inplace_frame(void) {
    struct LargeOrderBookFrame a = { .seq = 1 };
    struct LargeOrderBookFrame b = a; // Banned intermediate copy!
    (void)b;
}
