// Positive test: compile-time vregex lowered via Thompson NFA + Subset Construction + Hopcroft Minimization into .rodata DFA table
#include "veriobjc_runtime.h"

bool validate_ticker_symbol(const char *buf, size_t len) {
    return vregex_match("[A-Z]+[0-9]+", buf, len);
}
