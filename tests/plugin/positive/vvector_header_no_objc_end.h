#pragma once
#include "veriobjc_runtime.h"
#include "vstd/vector.h"

// Pure header with zero @interface / @end declarations:
VVector_decl(int32_t, 8);
VVector_decl(int32_t, 16);

static inline const char *vvector_header_literal_probe(void) {
    return "vvector<int, 4>";
}

static inline VVector(int32_t, 8) build_header_vec8(int32_t base) {
    VVector(int32_t, 8) out;
    vvector_init(&out);
    for (int32_t i = 0; i < 8; ++i) {
        (void)vvector_push(&out, base + i);
    }
    return out;
}

static inline VVector(int32_t, 16) build_header_vec16(int32_t base) {
    VVector(int32_t, 16) out;
    vvector_init(&out);
    for (int32_t i = 0; i < 16; ++i) {
        (void)vvector_push(&out, base * 2 + i);
    }
    return out;
}
