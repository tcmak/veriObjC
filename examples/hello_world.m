// examples/hello_world.m
// Task 3.15 Hosted Minimal-Binary Proof:
//   - Zero <vstd/*.h> imports (links against rt/ alone)
//   - Foreign printf declared @io @cold and called from @cold main
#include "veriobjc_runtime.h"

veriobjc_effect_io veriobjc_cold extern int printf(const char *fmt, ...);

veriobjc_cold int hello_world_entry(void) {
    printf("VeriObjC minimal hosted hello_world\n");
    return 0;
}

#ifdef VERIOBJC_EXAMPLE_MAIN
veriobjc_cold int main(void) {
    return hello_world_entry();
}
#endif
