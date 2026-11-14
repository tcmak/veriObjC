#pragma once
#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <sys0/mem.h> — Freestanding Memory Substrate & Non-Temporal Stores
// Layer: sys0 (depends on: rt)
// ---------------------------------------------------------------------------

static inline void *sys0_memset(void *dst, int c, size_t n) {
    if (!dst) {
        return NULL;
    }
    uint8_t *d = (uint8_t *)dst;
    uint8_t byte_val = (uint8_t)c;
    for (size_t i = 0u; i < n; ++i) {
        d[i] = byte_val;
    }
    return dst;
}

static inline void *sys0_memcpy(void *dst, const void *src, size_t n) {
    if (!dst || !src) {
        return dst;
    }
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    for (size_t i = 0u; i < n; ++i) {
        d[i] = s[i];
    }
    return dst;
}

static inline void *sys0_memcpy_nt(void *dst, const void *src, size_t n) {
    if (!dst || !src) {
        return dst;
    }
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    size_t i = 0u;
    for (; i + sizeof(uint64_t) <= n; i += sizeof(uint64_t)) {
        uint64_t word = 0ULL;
        __builtin_memcpy(&word, s + i, sizeof(uint64_t));
        uint64_t *aligned_dst = (uint64_t *)(void *)(d + i);
        __builtin_nontemporal_store(word, aligned_dst);
    }
    for (; i < n; ++i) {
        d[i] = s[i];
    }
    return dst;
}

static inline int sys0_memcmp(const void *s1, const void *s2, size_t n) {
    if (!s1 || !s2) {
        return 0;
    }
    const uint8_t *a = (const uint8_t *)s1;
    const uint8_t *b = (const uint8_t *)s2;
    for (size_t i = 0u; i < n; ++i) {
        if (a[i] != b[i]) {
            return (int)a[i] - (int)b[i];
        }
    }
    return 0;
}

#ifdef __cplusplus
}
#endif
