#pragma once
#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <vstd/map.h> — Cache-Aligned Sorted Flat Map (vflat_map<K, V, N>)
// Layer: vstd (depends on: rt)
//
// Invariants:
//   1. Keys and values sit in two parallel inline arrays (zero pointer chasing).
//   2. Keys are kept strictly sorted in ascending order for deterministic
//      iteration and branch-predictable binary search.
// ---------------------------------------------------------------------------

#define vflat_map_def(MapName, KeyType, ValType, Cap) \
    typedef struct __attribute__((annotate("objc_vflat_map:" #Cap), annotate("objc_flat"))) { \
        uint32_t count; \
        uint32_t cap; \
        KeyType  keys[Cap]; \
        ValType  vals[Cap]; \
    } MapName

static inline void __vflat_map_init_raw(void *map_obj, uint32_t *count_ptr, uint32_t *cap_ptr, uint32_t cap_val) {
    if (!map_obj) {
        return;
    }
    *count_ptr = 0u;
    *cap_ptr = cap_val;
}

static inline bool __vflat_map_insert_u64_i32(
    void *map_obj,
    uint32_t *count_ptr,
    uint32_t cap_val,
    uint64_t *keys,
    int32_t *vals,
    uint64_t key,
    int32_t val
) {
    if (!map_obj) {
        return false;
    }
    uint32_t n = *count_ptr;
    uint32_t pos = 0u;
    while (pos < n && keys[pos] < key) {
        pos++;
    }
    if (pos < n && keys[pos] == key) {
        vals[pos] = val;
        return true;
    }
    if (n >= cap_val) {
        return false;
    }
    for (uint32_t i = n; i > pos; --i) {
        keys[i] = keys[i - 1u];
        vals[i] = vals[i - 1u];
    }
    keys[pos] = key;
    vals[pos] = val;
    *count_ptr = n + 1u;
    return true;
}

static inline int32_t *__vflat_map_find_u64_i32(
    void *map_obj,
    uint32_t count_val,
    const uint64_t *keys,
    int32_t *vals,
    uint64_t key
) {
    if (!map_obj) {
        return NULL;
    }
    uint32_t lo = 0u;
    uint32_t hi = count_val;
    while (lo < hi) {
        uint32_t mid = lo + ((hi - lo) >> 1u);
        uint64_t mid_key = keys[mid];
        if (mid_key == key) {
            return &vals[mid];
        }
        if (mid_key < key) {
            lo = mid + 1u;
        } else {
            hi = mid;
        }
    }
    return NULL;
}

#define vflat_map_init(map_ptr) \
    __vflat_map_init_raw( \
        (map_ptr), \
        (map_ptr) ? &(map_ptr)->count : (uint32_t *)NULL, \
        (map_ptr) ? &(map_ptr)->cap : (uint32_t *)NULL, \
        (uint32_t)(sizeof((map_ptr)->keys) / sizeof((map_ptr)->keys[0])))

#define vflat_map_insert_u64(map_ptr, key_val, val_item) \
    __vflat_map_insert_u64_i32( \
        (map_ptr), \
        (map_ptr) ? &(map_ptr)->count : (uint32_t *)NULL, \
        (map_ptr) ? (map_ptr)->cap : 0u, \
        (map_ptr) ? (map_ptr)->keys : (uint64_t *)NULL, \
        (map_ptr) ? (map_ptr)->vals : (int32_t *)NULL, \
        (uint64_t)(key_val), \
        (int32_t)(val_item))

#define vflat_map_find_u64(map_ptr, key_val) \
    __vflat_map_find_u64_i32( \
        (map_ptr), \
        (map_ptr) ? (map_ptr)->count : 0u, \
        (map_ptr) ? (map_ptr)->keys : (const uint64_t *)NULL, \
        (map_ptr) ? (map_ptr)->vals : (int32_t *)NULL, \
        (uint64_t)(key_val))

#ifdef __cplusplus
}
#endif
