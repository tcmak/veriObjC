// examples/host_staged_file_loader.m
// Demonstrates Task 6.0.5 (<veriobjc_host_io.h> + <vstd/span.h>):
//   - @cold static staging file loader reading into caller-owned static memory (zero malloc/free)
//   - Branch-proven .is_ok check before .value access and handoff to @pure @nolatency kernel
#include "veriobjc_runtime.h"
#include "veriobjc_host_io.h"
#include "vstd/span.h"

vspan_def(StagedByteSpan, const uint8_t);

static uint8_t g_config_staging[512];

veriobjc_pure veriobjc_nolatency
uint32_t fold_staged_fnv1a(StagedByteSpan bytes) {
    uint32_t h = 0x811c9dc5u;
    for (size_t i = 0u; i < bytes.len; ++i) {
        const uint8_t *b = vspan_at(&bytes, i);
        if (b) {
            h ^= (uint32_t)(*b);
            h = (h << 1u) ^ h;
        }
    }
    return h;
}

veriobjc_cold
int32_t bootstrap_staged_config_hash(const char *path, uint32_t *out_hash) {
    struct veriobjc_host_io_result res =
        veriobjc_host_load_file_staged(path, g_config_staging, sizeof(g_config_staging));
    if (!res.is_ok) {
        return -(int32_t)res.err;
    }
    StagedByteSpan span = veriobjc_host_slice_as_span(StagedByteSpan, res.value);
    if (out_hash) {
        *out_hash = fold_staged_fnv1a(span);
    }
    return (int32_t)span.len;
}
