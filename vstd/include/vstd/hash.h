#pragma once
#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <vstd/hash.h> — Open-Addressing Flat Hash Table (vhash_flat<K, V, CapPow2>)
// Layer: vstd (depends on: rt)
//
// Invariants:
//   1. Capacity is a power of two <= 64 (occupancy tracked in a 64-bit bitmap).
//   2. Load-factor ceiling is 75% ((CapPow2 * 3) / 4); exceeding the ceiling
//      is rejected at compile time by VeriObjCPlugin and returns false at
//      runtime without ever reallocating or rehashing on the heap.
// ---------------------------------------------------------------------------

#define vhash_flat_def(HashName, KeyType, ValType, CapPow2) \
    typedef struct __attribute__((annotate("objc_vhash_flat:" #CapPow2), annotate("objc_flat"))) { \
        uint64_t occupied_mask; \
        uint32_t count; \
        uint32_t cap; \
        KeyType  keys[CapPow2]; \
        ValType  vals[CapPow2]; \
    } HashName

static inline void __vhash_flat_init_raw(
    void *map_obj,
    uint64_t *mask_ptr,
    uint32_t *count_ptr,
    uint32_t *cap_ptr,
    uint32_t cap_val
) {
    if (!map_obj) {
        return;
    }
    *mask_ptr = 0ULL;
    *count_ptr = 0u;
    *cap_ptr = cap_val;
}

static inline uint32_t __vhash_slot_u64(uint64_t key, uint32_t cap_mask) {
    return (uint32_t)(key & (uint64_t)cap_mask);
}

static inline bool __vhash_flat_insert_u64_i32(
    void *map_obj,
    uint64_t *occupied_mask,
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
    uint32_t mask = cap_val - 1u;
    uint32_t max_load = (cap_val * 3u) >> 2u;

    for (uint32_t slot = __vhash_slot_u64(key, mask);; slot = (slot + 1u) & mask) {
        uint64_t bit = 1ULL << slot;
        if ((*occupied_mask & bit) == 0ULL) {
            if (*count_ptr >= max_load) {
                return false;
            }
            *occupied_mask |= bit;
            keys[slot] = key;
            vals[slot] = val;
            (*count_ptr)++;
            return true;
        }
        if (keys[slot] == key) {
            vals[slot] = val;
            return true;
        }
    }
}

static inline int32_t *__vhash_flat_find_u64_i32(
    void *map_obj,
    uint64_t occupied_mask,
    uint32_t cap_val,
    const uint64_t *keys,
    int32_t *vals,
    uint64_t key
) {
    if (!map_obj) {
        return NULL;
    }
    uint32_t mask = cap_val - 1u;

    for (uint32_t slot = __vhash_slot_u64(key, mask);; slot = (slot + 1u) & mask) {
        uint64_t bit = 1ULL << slot;
        if ((occupied_mask & bit) == 0ULL) {
            return NULL;
        }
        if (keys[slot] == key) {
            return &vals[slot];
        }
    }
}

#define vhash_flat_init(map_ptr) \
    __vhash_flat_init_raw( \
        (map_ptr), \
        (map_ptr) ? &(map_ptr)->occupied_mask : (uint64_t *)NULL, \
        (map_ptr) ? &(map_ptr)->count : (uint32_t *)NULL, \
        (map_ptr) ? &(map_ptr)->cap : (uint32_t *)NULL, \
        (uint32_t)(sizeof((map_ptr)->keys) / sizeof((map_ptr)->keys[0])))

#define vhash_flat_insert_u64(map_ptr, key_val, val_item) \
    __vhash_flat_insert_u64_i32( \
        (map_ptr), \
        (map_ptr) ? &(map_ptr)->occupied_mask : (uint64_t *)NULL, \
        (map_ptr) ? &(map_ptr)->count : (uint32_t *)NULL, \
        (map_ptr) ? (map_ptr)->cap : 0u, \
        (map_ptr) ? (map_ptr)->keys : (uint64_t *)NULL, \
        (map_ptr) ? (map_ptr)->vals : (int32_t *)NULL, \
        (uint64_t)(key_val), \
        (int32_t)(val_item))

#define vhash_flat_find_u64(map_ptr, key_val) \
    __vhash_flat_find_u64_i32( \
        (map_ptr), \
        (map_ptr) ? (map_ptr)->occupied_mask : 0ULL, \
        (map_ptr) ? (map_ptr)->cap : 0u, \
        (map_ptr) ? (map_ptr)->keys : (const uint64_t *)NULL, \
        (map_ptr) ? (map_ptr)->vals : (int32_t *)NULL, \
        (uint64_t)(key_val))

#ifdef __cplusplus
}
#endif
