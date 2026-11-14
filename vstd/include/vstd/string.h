#pragma once
#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <vstd/string.h> — Non-Allocating Immutable String Views & Bounded Buffers
// Layer: vstd (depends on: rt)
// ---------------------------------------------------------------------------

struct vstring_view {
    const char *data;
    size_t      len;
};

static inline struct vstring_view vstring_view_empty(void) {
    struct vstring_view v = { .data = "", .len = 0u };
    return v;
}

static inline struct vstring_view vstring_view_init(const char *data, size_t len) {
    if (!data) {
        return vstring_view_empty();
    }
    struct vstring_view v = { .data = data, .len = len };
    return v;
}

static inline struct vstring_view vstring_view_from_cstr(const char *cstr) {
    if (!cstr) {
        return vstring_view_empty();
    }
    size_t n = 0u;
    while (cstr[n] != '\0') {
        n++;
    }
    struct vstring_view v = { .data = cstr, .len = n };
    return v;
}

#define vstring_view_FromLiteral(lit) \
    vstring_view_init((lit), sizeof(lit) - 1u)

static inline bool vstring_view_is_empty(struct vstring_view v) {
    return v.len == 0u;
}

static inline char vstring_view_at(struct vstring_view v, size_t idx) {
    if (!v.data || idx >= v.len) {
        return '\0';
    }
    return v.data[idx];
}

static inline struct vstring_view vstring_view_slice(struct vstring_view v, size_t offset, size_t count) {
    if (!v.data || offset >= v.len) {
        return vstring_view_empty();
    }
    size_t avail = v.len - offset;
    size_t actual = (count <= avail) ? count : avail;
    struct vstring_view sub = { .data = v.data + offset, .len = actual };
    return sub;
}

static inline bool vstring_view_eq(struct vstring_view a, struct vstring_view b) {
    if (a.len != b.len) {
        return false;
    }
    for (size_t i = 0u; i < a.len; ++i) {
        if (a.data[i] != b.data[i]) {
            return false;
        }
    }
    return true;
}

static inline bool vstring_view_starts_with(struct vstring_view v, struct vstring_view prefix) {
    if (prefix.len > v.len) {
        return false;
    }
    struct vstring_view head = vstring_view_slice(v, 0u, prefix.len);
    return vstring_view_eq(head, prefix);
}

// Bounded inline text buffer: vfixed_string_def(TypeName, Capacity)
#define vfixed_string_def(Name, Cap) \
    typedef struct { \
        size_t len; \
        char   buf[Cap]; \
    } Name

static inline void __vfixed_string_init_raw(size_t *len_ptr, char *buf_ptr, size_t cap) {
    if (!len_ptr) {
        return;
    }
    *len_ptr = 0u;
    for (size_t i = 0u; i < cap; ++i) {
        buf_ptr[i] = '\0';
    }
}

static inline bool __vfixed_string_copy_raw(size_t *len_ptr, char *buf_ptr, size_t cap, struct vstring_view src) {
    if (!len_ptr || src.len > cap) {
        return false;
    }
    for (size_t i = 0u; i < src.len; ++i) {
        buf_ptr[i] = src.data[i];
    }
    *len_ptr = src.len;
    return true;
}

static inline struct vstring_view __vfixed_string_view_raw(const void *obj_ptr, const size_t *len_ptr, const char *buf_ptr) {
    if (!obj_ptr) {
        return vstring_view_empty();
    }
    return vstring_view_init(buf_ptr, *len_ptr);
}

#define vfixed_string_init(fs_ptr) \
    __vfixed_string_init_raw((fs_ptr) ? &(fs_ptr)->len : (size_t *)NULL, \
                             (fs_ptr) ? (fs_ptr)->buf : (char *)NULL, \
                             sizeof((fs_ptr)->buf))

#define vfixed_string_copy(fs_ptr, view_val) \
    __vfixed_string_copy_raw((fs_ptr) ? &(fs_ptr)->len : (size_t *)NULL, \
                             (fs_ptr) ? (fs_ptr)->buf : (char *)NULL, \
                             sizeof((fs_ptr)->buf), \
                             (view_val))

#define vfixed_string_view(fs_ptr) \
    __vfixed_string_view_raw((fs_ptr), \
                             (fs_ptr) ? &(fs_ptr)->len : (const size_t *)NULL, \
                             (fs_ptr) ? (fs_ptr)->buf : (const char *)NULL)

#ifdef __cplusplus
}
#endif
