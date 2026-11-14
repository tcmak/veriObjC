#pragma once
#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <vstd/span.h> — Zero-Copy Contiguous Bounds-Checked Slices (vspan<T>)
// Layer: vstd (depends on: rt)
// ---------------------------------------------------------------------------

#define vspan_def(SpanName, ElemType) \
    typedef struct { \
        ElemType *data; \
        size_t    len; \
    } SpanName

static inline void __vspan_init_raw(void **out_data, size_t *out_len, void *ptr, size_t count) {
    if (!ptr) {
        *out_data = NULL;
        *out_len = 0u;
        return;
    }
    *out_data = ptr;
    *out_len = count;
}

static inline void *__vspan_at_raw(const void *span_obj, void *data, size_t len, size_t elem_size, size_t idx) {
    if (!span_obj || idx >= len) {
        return NULL;
    }
    return (void *)((uint8_t *)data + idx * elem_size);
}

static inline void *__vspan_subspan_data(void *data, size_t len, size_t elem_size, size_t offset) {
    if (offset >= len) {
        return NULL;
    }
    return (void *)((uint8_t *)data + offset * elem_size);
}

static inline size_t __vspan_subspan_len(size_t len, size_t offset, size_t count) {
    if (offset >= len) {
        return 0u;
    }
    size_t avail = len - offset;
    return (count <= avail) ? count : avail;
}

#define vspan_init(span_ptr, ptr, count) \
    __vspan_init_raw((void **)&(span_ptr)->data, &(span_ptr)->len, (void *)(ptr), (size_t)(count))

#define vspan_from_array(SpanName, arr) \
    ((SpanName){ .data = &(arr)[0], .len = sizeof(arr) / sizeof((arr)[0]) })

#define vspan_at(span_ptr, idx) \
    ((__typeof__((span_ptr)->data))__vspan_at_raw( \
        (span_ptr), \
        (span_ptr) ? (void *)(span_ptr)->data : NULL, \
        (span_ptr) ? (span_ptr)->len : 0u, \
        sizeof(*(span_ptr)->data), \
        (size_t)(idx)))

#define vspan_subspan(SpanName, span_val, offset, count) \
    ((SpanName){ \
        .data = (__typeof__((span_val).data))__vspan_subspan_data( \
            (void *)(span_val).data, (span_val).len, sizeof(*(span_val).data), (size_t)(offset)), \
        .len  = __vspan_subspan_len((span_val).len, (size_t)(offset), (size_t)(count)) \
    })

#ifdef __cplusplus
}
#endif
