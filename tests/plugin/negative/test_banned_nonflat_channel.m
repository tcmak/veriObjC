// Negative test: sending a non-flat struct (containing a pointer field) across a channel is banned
#include "veriobjc_runtime.h"

typedef struct {
    uint32_t stream_id;
    uint8_t *raw_buffer_ptr; // Forbidden pointer inside channel payload!
} NonFlatFrame;

__attribute__((objc_flat))
typedef struct {
    uint32_t header;
    NonFlatFrame nested;
} NonFlatEnvelope;

void test_nonflat_send(NonFlatEnvelope *env) {
    (void)env;
}
