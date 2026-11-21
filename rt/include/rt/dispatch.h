#ifndef VERIOBJC_RT_DISPATCH_H
#define VERIOBJC_RT_DISPATCH_H

#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <rt/dispatch.h> — Freestanding Closed-World @sealed Dispatch Primitives
// Layer: rt (zero vstd, sys0, hw, host, float, or libc dependencies)
//
// Layout invariant:
//   veriobjc_sealed_alloc lays down an 8-byte aligned prefix header
//   (struct veriobjc_sealed_header) immediately BEFORE the returned object
//   pointer `self` (((const struct veriobjc_sealed_header *)self)[-1]), so:
//     1. Clang ivar offsets starting at byte 0 of `self` remain exact across
//        mixed-size @sealed base and subclass hierarchies (zero slicing).
//     2. Upcasting `Base *p = sub;` requires zero pointer adjustment.
//     3. Synthesized static switch dispatch reads `type_tag` at `self - 8`.
//     4. Lexical VERIOBJC_SCOPED_STACK teardown resets the entire arena in
//        one single store instruction.
// ---------------------------------------------------------------------------

typedef uint16_t veriobjc_type_tag_t;

struct veriobjc_sealed_header {
    veriobjc_type_tag_t type_tag;
    uint16_t            reserved_flags;
    uint32_t            payload_bytes;
};

static inline veriobjc_type_tag_t veriobjc_type_tag_from_name(const char *cls_name) {
    if (!cls_name) {
        return 0u;
    }
    if (*cls_name == '\0') {
        return 0u;
    }
    uint32_t h = veriobjc_sel_hash(cls_name);
    return (veriobjc_type_tag_t)(((h >> 16u) ^ (h & 0xFFFFu)) | 1u);
}

static inline void *veriobjc_sealed_alloc(
    struct veriobjc_stack *s,
    size_t instance_size,
    veriobjc_type_tag_t type_tag
) {
    if (!s) {
        return NULL;
    }
    if (type_tag == 0u) {
        return NULL;
    }
    if (instance_size > SIZE_MAX - sizeof(struct veriobjc_sealed_header)) {
        return NULL;
    }
    size_t total = sizeof(struct veriobjc_sealed_header) + instance_size;
    uint8_t *raw = (uint8_t *)veriobjc_stack_alloc(s, total);
    if (!raw) {
        return NULL;
    }
    for (size_t i = 0u; i < total; ++i) {
        raw[i] = 0u;
    }
    struct veriobjc_sealed_header *hdr = (struct veriobjc_sealed_header *)raw;
    hdr->type_tag = type_tag;
    hdr->reserved_flags = 0u;
    hdr->payload_bytes = (uint32_t)instance_size;
    return (void *)(hdr + 1);
}

static inline veriobjc_type_tag_t veriobjc_sealed_tag(const void *obj) {
    if (!obj) {
        return 0u;
    }
    const struct veriobjc_sealed_header *hdr = ((const struct veriobjc_sealed_header *)obj) - 1;
    return hdr->type_tag;
}

#define veriobjc_alloc_sealed(Cls, stack_ptr) \
    ((Cls *)veriobjc_sealed_alloc((stack_ptr), sizeof(Cls), veriobjc_type_tag_from_name(#Cls)))

static inline veriobjc_imp_t veriobjc_dispatch_by_sel_name(
    const struct veriobjc_object *obj,
    const char *sel_name
) {
    if (!obj || !sel_name) {
        return NULL;
    }
    return veriobjc_object_dispatch(obj, veriobjc_sel_hash(sel_name));
}

#ifdef __cplusplus
}
#endif

#endif /* VERIOBJC_RT_DISPATCH_H */
