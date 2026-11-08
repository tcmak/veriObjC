// Negative test: a vconstexpr function that exceeds the compile-time step or recursion depth fuel budget is rejected cleanly
#include "veriobjc_runtime.h"

vconstexpr uint32_t infinite_step_explosion(uint32_t n) {
    return infinite_step_explosion(n + 1u) + 1u;
}

uint32_t trigger_fuel_exhaustion(void) {
    return infinite_step_explosion(1u);
}
