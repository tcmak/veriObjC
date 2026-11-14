#pragma once
#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <sys0/boot.h> — Per-Core Boot Descriptors & Hardware TLS Floor
// Layer: sys0 (depends on: rt)
// ---------------------------------------------------------------------------

struct sys0_core_boot_info {
    uint16_t core_id;
    uint16_t numa_node;
    void    *tls_base;
    size_t   tls_size;
};

static inline void sys0_boot_init(
    struct sys0_core_boot_info *info,
    uint16_t core_id,
    uint16_t numa_node,
    void *tls_base,
    size_t tls_size
) {
    if (!info) {
        return;
    }
    info->core_id   = core_id;
    info->numa_node = numa_node;
    info->tls_base  = tls_base;
    info->tls_size  = tls_size;
}

static inline void *sys0_tls_base(const struct sys0_core_boot_info *info) {
    if (!info) {
        return NULL;
    }
    return info->tls_base;
}

#ifdef __cplusplus
}
#endif
