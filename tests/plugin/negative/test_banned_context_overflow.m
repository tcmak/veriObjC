// Negative test: exceeding the declared BRAM KV-cache ContextCap is a compile error
#include "veriobjc_runtime.h"

void overflow_kv_context(void) {
    vstream_check_context(4096u, 2048u);
}
