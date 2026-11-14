// EXPECTED_DIAGNOSTIC: VeriObjC Violation: Context cap exceeded — compile error
#include "veriobjc_runtime.h"
#include "vstd/stream.h"

vstream_kv_arena_def(BoundedKVArena4, int32_t, 2, 4);

void append_beyond_context_cap(void) {
    BoundedKVArena4 kv;
    vstream_kv_arena_init(&kv);
    int32_t pair[2] = {1, 2};
    // Static token index 4 >= ContextCap (4) must be rejected at compile time
    vstream_kv_append(&kv, 4u, pair);
}
