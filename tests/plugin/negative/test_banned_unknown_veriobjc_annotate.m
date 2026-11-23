// Negative plugin test (Task 6.0.4): reject raw unknown annotate("veriobjc_*") / annotate("objc_*")
// spellings that would otherwise silently fall through to default @nolatency checking.
#include <stdint.h>

// Misspelled raw annotate("veriobjc_cold") instead of canonical macro `veriobjc_cold`
__attribute__((annotate("veriobjc_cold")))
int32_t legacy_init_loader(int32_t x) {
    return x + 1;
}

// Misspelled raw annotate("objc_purr") instead of canonical `veriobjc_pure`
__attribute__((annotate("objc_purr")))
int32_t typo_pure_helper(int32_t x) {
    return x + 2;
}
