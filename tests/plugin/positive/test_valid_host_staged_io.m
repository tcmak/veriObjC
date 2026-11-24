// Positive plugin test (Task 6.0.5): @cold static staging file loader in host/include/veriobjc_host_io.h
#include "veriobjc_runtime.h"
#include "veriobjc_host_io.h"
#include "vstd/span.h"

vspan_def(ConstStagingSpan, const uint8_t);

static uint8_t g_file_staging[256];

veriobjc_pure veriobjc_nolatency
uint32_t checksum_staged_bytes(ConstStagingSpan bytes) {
    uint32_t acc = 0u;
    for (size_t i = 0u; i < bytes.len; ++i) {
        const uint8_t *b = vspan_at(&bytes, i);
        if (b) {
            acc = (acc << 3u) ^ (uint32_t)(*b);
        }
    }
    return acc;
}

veriobjc_cold
int32_t load_and_verify_config_blob(const char *path, uint32_t *out_crc) {
    struct veriobjc_host_io_result res =
        veriobjc_host_load_file_staged(path, g_file_staging, sizeof(g_file_staging));
    if (!res.is_ok) {
        return -(int32_t)res.err;
    }
    ConstStagingSpan slice = veriobjc_host_slice_as_span(ConstStagingSpan, res.value);
    if (out_crc) {
        *out_crc = checksum_staged_bytes(slice);
    }
    return (int32_t)slice.len;
}
