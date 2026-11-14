#pragma once
#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <vstd/variant.h> — Closed Algebraic Sum Types (vvariant + vmatch)
// Layer: vstd (depends on: rt)
//
// Invariants:
//   1. Exact layout = discriminant tag + union of inline payloads (zero heap).
//   2. vmatch(v) lowers to an indexed switch over v.tag and is verified for
//      exhaustiveness across all enum constants by VeriObjCPlugin.
// ---------------------------------------------------------------------------

#define vvariant_def(VariantName, TagEnumType, UnionBody) \
    typedef struct __attribute__((annotate("objc_variant"), annotate("objc_flat"))) { \
        TagEnumType tag; \
        union UnionBody payload; \
    } VariantName

static inline uint32_t __vvariant_tag_id(uint32_t tag_val) {
    return tag_val;
}

#define vvariant_make(VariantName, tag_val, field_name, field_val) \
    ((VariantName){ .tag = (tag_val), .payload = { .field_name = (field_val) } })

#define vvariant_tag(var_val) \
    __vvariant_tag_id((uint32_t)(var_val).tag)

#undef vmatch
#define vmatch(var_val) \
    switch (vvariant_tag(var_val))

#ifdef __cplusplus
}
#endif
