// EXPECTED_DIAGNOSTIC: VeriObjC Violation: Context cap exceeded — compile error
// Valid C/Objective-C call, banned in VeriObjC when appending at a static token index >= ContextCap.
#include "veriobjc_runtime.h"
#include "vstd/stream.h"

vstream_kv_arena_def(BoundedKVArena4, int32_t, 2, 4);

void append_beyond_context_cap(void) {
    BoundedKVArena4 kv;
    vstream_kv_arena_init(&kv);
    int32_t pair[2] = {1, 2};
    vstream_kv_append(&kv, 4u, pair);
}
