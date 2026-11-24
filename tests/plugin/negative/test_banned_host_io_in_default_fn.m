// Negative plugin test (Task 6.0.5):
//   1. Calling @cold veriobjc_host_load_file_staged from a default (non-@cold) or @nolatency function fails closed.
//   2. Passing a veriobjc_stack_alloc pointer across an opaque libc fread FFI boundary remains banned.
#include "veriobjc_runtime.h"
#include "veriobjc_host_io.h"

static uint8_t g_staging[64];

// 1. Default (unannotated) function attempting to call @cold veriobjc_host_load_file_staged
int32_t illegal_call_host_loader_in_default_fn(const char *path) {
    struct veriobjc_host_io_result r =
        veriobjc_host_load_file_staged(path, g_staging, sizeof(g_staging));
    return r.is_ok ? 1 : 0;
}

// 2. Explicit @nolatency function attempting to call @cold veriobjc_host_load_file_staged
veriobjc_nolatency
int32_t illegal_call_host_loader_in_nolatency_fn(const char *path) {
    struct veriobjc_host_io_result r =
        veriobjc_host_load_file_staged(path, g_staging, sizeof(g_staging));
    return r.is_ok ? 1 : 0;
}

// 3. Passing veriobjc_stack_alloc pointer across opaque libc fread FFI boundary
veriobjc_cold
size_t illegal_fread_into_stack_alloc(struct veriobjc_stack *s, FILE *fp) {
    uint8_t *scratch = (uint8_t *)veriobjc_stack_alloc(s, 32u);
    return fread(scratch, 1u, 32u, fp);
}
