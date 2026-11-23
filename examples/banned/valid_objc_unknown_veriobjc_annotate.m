// Valid stock Objective-C: raw __attribute__((annotate("veriobjc_cold"))) spelling.
// Compiles cleanly under plain Clang 23 (-Werror), but rejected by VeriObjC (Task 6.0.4)
// so misspelled raw annotations never silently bypass @cold/@pure/@nolatency checking.
#include <stdint.h>

__attribute__((annotate("veriobjc_cold")))
int32_t misannotated_cold_bootstrap(int32_t cfg_code) {
    return cfg_code + 10;
}
