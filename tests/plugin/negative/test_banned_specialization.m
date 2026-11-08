// Negative test: ad-hoc generic specialization / SFINAE probing is banned
#include "veriobjc_runtime.h"

veriobjc_specialize
int specialized_probe_fn(int x) {
    return x + 1;
}
