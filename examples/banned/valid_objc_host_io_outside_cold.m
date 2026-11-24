// Valid stock Objective-C: calling @cold veriobjc_host_load_file_staged from a non-@cold function.
// Compiles cleanly under plain Clang 23 (-Werror), but rejected by VeriObjC (Task 6.0.5)
// because @cold hosted file I/O is quarantined to explicit veriobjc_cold entrypoints.
#include "veriobjc_runtime.h"
#include "veriobjc_host_io.h"

static uint8_t g_staging[128];

int32_t hot_path_accidental_file_load(const char *path) {
    struct veriobjc_host_io_result r =
        veriobjc_host_load_file_staged(path, g_staging, sizeof(g_staging));
    if (!r.is_ok) {
        return -1;
    }
    return (int32_t)r.value.len;
}
