#ifndef VERIOBJC_HOST_IO_H
#define VERIOBJC_HOST_IO_H

#include "veriobjc_runtime.h"

#if defined(__STDC_HOSTED__) && (__STDC_HOSTED__ == 1)
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <veriobjc_host_io.h> — @cold Static Staging File Loader (Task 6.0.5)
// Layer: host (depends on: rt + hosted <stdio.h>; zero vstd/sys0/hw imports)
//
// Invariants:
//   1. Annotated `@cold @io @blocking` (`objc_cold`, `objc_effect_io`,
//      `objc_effect_blocking`) so calls from default or `@nolatency`/`@pure`
//      functions fail closed at compile time.
//   2. Reads strictly into caller-supplied static/outer staging memory with
//      zero `malloc` / `free`, failing closed on missing, empty (0-byte),
//      oversized (`file_size > staging_cap`), or short-read files.
//   3. Result struct carries `objc_vresult` so `.value` access without an
//      enclosing `.is_ok` branch check is rejected by VeriObjCPlugin.
// ---------------------------------------------------------------------------

typedef enum veriobjc_host_io_err {
    VERIOBJC_HOST_IO_OK              = 0u,
    VERIOBJC_HOST_IO_ERR_INVALID_ARG = 1u,
    VERIOBJC_HOST_IO_ERR_OPEN        = 2u,
    VERIOBJC_HOST_IO_ERR_SEEK_TELL   = 3u,
    VERIOBJC_HOST_IO_ERR_EMPTY       = 4u,
    VERIOBJC_HOST_IO_ERR_TOO_LARGE   = 5u,
    VERIOBJC_HOST_IO_ERR_SHORT_READ  = 6u
} veriobjc_host_io_err_t;

struct veriobjc_host_byte_slice {
    const uint8_t *data;
    size_t         len;
};

struct __attribute__((annotate("objc_vresult"))) veriobjc_host_io_result {
    bool                            is_ok;
    struct veriobjc_host_byte_slice value;
    veriobjc_host_io_err_t          err;
};

#define veriobjc_host_slice_as_span(SpanName, slice_expr) \
    ((SpanName){ .data = (slice_expr).data, .len = (slice_expr).len })

__attribute__((annotate("objc_cold"), annotate("objc_effect_io"), annotate("objc_effect_blocking")))
static inline struct veriobjc_host_io_result veriobjc_host_load_file_staged(
    const char *path,
    uint8_t *staging_buf,
    size_t staging_cap
) {
    struct veriobjc_host_io_result fail_res = {
        .is_ok = false,
        .value = { .data = NULL, .len = 0u },
        .err   = VERIOBJC_HOST_IO_ERR_INVALID_ARG
    };
    if (!path || path[0] == '\0' || !staging_buf || staging_cap == 0u) {
        return fail_res;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        fail_res.err = VERIOBJC_HOST_IO_ERR_OPEN;
        return fail_res;
    }

    if (fseek(fp, 0L, SEEK_END) != 0) {
        fclose(fp);
        fail_res.err = VERIOBJC_HOST_IO_ERR_SEEK_TELL;
        return fail_res;
    }

    long raw_sz = ftell(fp);
    if (raw_sz < 0L || fseek(fp, 0L, SEEK_SET) != 0) {
        fclose(fp);
        fail_res.err = VERIOBJC_HOST_IO_ERR_SEEK_TELL;
        return fail_res;
    }

    if (raw_sz == 0L) {
        fclose(fp);
        fail_res.err = VERIOBJC_HOST_IO_ERR_EMPTY;
        return fail_res;
    }

    size_t file_sz = (size_t)raw_sz;
    if (file_sz > staging_cap) {
        fclose(fp);
        fail_res.err = VERIOBJC_HOST_IO_ERR_TOO_LARGE;
        return fail_res;
    }

    size_t nread = fread(staging_buf, 1u, file_sz, fp);
    int trailing = fgetc(fp);
    fclose(fp);

    if (nread != file_sz || trailing != EOF) {
        fail_res.err = VERIOBJC_HOST_IO_ERR_SHORT_READ;
        return fail_res;
    }

    struct veriobjc_host_io_result ok_res = {
        .is_ok = true,
        .value = { .data = staging_buf, .len = nread },
        .err   = VERIOBJC_HOST_IO_OK
    };
    return ok_res;
}

#ifdef __cplusplus
}
#endif

#endif /* __STDC_HOSTED__ */

#endif /* VERIOBJC_HOST_IO_H */
