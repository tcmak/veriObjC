// Negative test: regex pattern exceeding the compile-time DFA byte budget is rejected
#include "veriobjc_runtime.h"

bool match_oversized_dfa(const char *buf, size_t len) {
    return vregex_match("ABCDEFGHIJKLMNOPQRST", buf, len);
}
