#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Structural Object Invariants
// ---------------------------------------------------------------------------

struct veriobjc_class;

// Structural root of all objects.
struct veriobjc_object {
    struct veriobjc_class *isa;
};

struct veriobjc_class {
    struct veriobjc_class *superclass;
    const char *name;
    size_t instance_size;
};

typedef struct veriobjc_object *id;
typedef struct veriobjc_class *Class;
typedef struct veriobjc_selector *SEL;

// ---------------------------------------------------------------------------
// Data Stacks: grow-only bump allocators.
// Scope exit resets 'top' in ONE store.
// Growth never moves data, ensuring reference stability.
// ---------------------------------------------------------------------------

#define VERIOBJC_DEFAULT_ALIGN sizeof(void*)

static inline size_t veriobjc_align_up(size_t n, size_t align) {
    if (n > SIZE_MAX - (align - 1)) {
        return SIZE_MAX;
    }
    return (n + (align - 1)) & ~(align - 1);
}

struct veriobjc_stack {
    uint8_t *base;   // Start of the reservation
    uint8_t *top;    // Bump pointer: the ONLY mutable state
    size_t   size;   // Total capacity in bytes
};

static inline void veriobjc_stack_init(struct veriobjc_stack *s, void *base, size_t n) {
    s->base = (uint8_t *)base;
    s->top = (uint8_t *)base;
    s->size = n;
}

static inline size_t veriobjc_stack_used(const struct veriobjc_stack *s) {
    return (size_t)(s->top - s->base);
}

static inline size_t veriobjc_stack_available(const struct veriobjc_stack *s) {
    size_t used = veriobjc_stack_used(s);
    return (used <= s->size) ? (s->size - used) : 0;
}

static inline void *veriobjc_stack_alloc(struct veriobjc_stack *s, size_t n) {
    if (!s) {
        return NULL;
    }
    size_t aligned_n = veriobjc_align_up(n, VERIOBJC_DEFAULT_ALIGN);
    if (aligned_n == SIZE_MAX) {
        return NULL;
    }
    size_t used = (size_t)(s->top - s->base);
    if (aligned_n > s->size || used > s->size - aligned_n) {
        return NULL;
    }
    uint8_t *p = s->top;
    s->top += aligned_n;
    return (void *)p;
}

static inline void veriobjc_stack_reset(struct veriobjc_stack *s, uint8_t *mark) {
    // THE free: exactly one store, however deeply nested the allocations were.
    s->top = mark;
}

// ---------------------------------------------------------------------------
// vpool: recycled slots over a grow-only array.
//
// Memory is NEVER released to an OS allocator. A freed slot is pushed onto
// a hidden freelist encoded at the start of the slot itself (as a uint32_t
// index). A stale hold on a slot index reads a different value of the CORRECT
// type — a logic bug, never memory corruption.
//
// Allocation O(1): pop freelist head, or bump the backing data stack.
// Free       O(1): push slot index onto the freelist head.
// at()       O(1): base + slot_index * slot_size — bounds-checked.
// ---------------------------------------------------------------------------

struct veriobjc_pool {
    struct veriobjc_stack *backing;  // pool storage lives on a data stack
    size_t   slot_size;              // sizeof(T), assigned statically
    uint32_t count;                  // high-water mark of used slots
    uint32_t free_head;              // UINT32_MAX = empty freelist
};

static inline void veriobjc_pool_init(struct veriobjc_pool *p,
                                      struct veriobjc_stack *s,
                                      size_t slot_size) {
    if (!p) { return; }
    p->backing   = s;
    p->slot_size = slot_size;
    p->count     = 0;
    p->free_head = UINT32_MAX;
}

static inline uint32_t veriobjc_pool_alloc(struct veriobjc_pool *p) {
    if (!p) { return UINT32_MAX; }

    if (p->free_head != UINT32_MAX) {
        // Pop the freelist head: the first sizeof(uint32_t) bytes of the slot
        // store the next free index while the slot is not in use.
        uint32_t slot = p->free_head;
        uint8_t *slot_ptr = p->backing->base + (size_t)slot * p->slot_size;
        uint32_t next;
        __builtin_memcpy(&next, slot_ptr, sizeof(uint32_t));
        p->free_head = next;
        return slot;
    }

    // No free slots: bump the backing data stack and return the new index.
    veriobjc_stack_alloc(p->backing, p->slot_size);
    return p->count++;
}

static inline void veriobjc_pool_free(struct veriobjc_pool *p, uint32_t slot) {
    if (!p) { return; }
    // Encode the current freelist head at the start of the slot's memory,
    // then make this slot the new head.
    uint8_t *slot_ptr = p->backing->base + (size_t)slot * p->slot_size;
    __builtin_memcpy(slot_ptr, &p->free_head, sizeof(uint32_t));
    p->free_head = slot;
}

// Returns a typed pointer to the slot at `index`, or NULL if out-of-bounds.
static inline void *veriobjc_pool_at(const struct veriobjc_pool *p, uint32_t index) {
    if (!p) { return NULL; }
    if (index >= p->count) { return NULL; }
    return (void *)(p->backing->base + (size_t)index * p->slot_size);
}

// ---------------------------------------------------------------------------
// vweak / Generation-Counted Pool (vmeta §6 & vstd §3)
//
// Detects stale slot access instead of silently returning same-typed garbage.
// Each slot carries an 8-byte header:
//   - generation: uint32_t (bumped on alloc and bumped on free; odd = active, even = free)
//   - next_free: uint32_t (freelist link when inactive)
// ---------------------------------------------------------------------------

struct veriobjc_handle {
    uint32_t slot;
    uint32_t generation;
};

struct veriobjc_gen_slot_header {
    uint32_t generation;
    uint32_t next_free;
};

struct veriobjc_gen_pool {
    struct veriobjc_stack *backing;
    size_t   payload_size;
    size_t   total_slot_size;
    uint32_t count;
    uint32_t free_head;
};

static inline void veriobjc_gen_pool_init(struct veriobjc_gen_pool *p,
                                          struct veriobjc_stack *s,
                                          size_t payload_size) {
    if (!p) { return; }
    p->backing = s;
    p->payload_size = payload_size;
    size_t header_size = sizeof(struct veriobjc_gen_slot_header);
    p->total_slot_size = header_size + veriobjc_align_up(payload_size, VERIOBJC_DEFAULT_ALIGN);
    p->count = 0;
    p->free_head = UINT32_MAX;
}

static inline struct veriobjc_handle veriobjc_gen_pool_alloc(struct veriobjc_gen_pool *p) {
    struct veriobjc_handle invalid = { .slot = UINT32_MAX, .generation = 0 };
    if (!p || !p->backing) { return invalid; }

    uint32_t slot;
    struct veriobjc_gen_slot_header *hdr;

    if (p->free_head != UINT32_MAX) {
        slot = p->free_head;
        uint8_t *slot_ptr = p->backing->base + (size_t)slot * p->total_slot_size;
        hdr = (struct veriobjc_gen_slot_header *)slot_ptr;
        p->free_head = hdr->next_free;
    } else {
        slot = p->count;
        void *mem = veriobjc_stack_alloc(p->backing, p->total_slot_size);
        if (!mem) { return invalid; }
        hdr = (struct veriobjc_gen_slot_header *)mem;
        hdr->generation = 0;
        p->count++;
    }

    hdr->generation++; // Active generation is odd (1, 3, 5...)
    struct veriobjc_handle h = { .slot = slot, .generation = hdr->generation };
    return h;
}

static inline bool veriobjc_gen_pool_is_valid(const struct veriobjc_gen_pool *p,
                                              struct veriobjc_handle handle) {
    if (!p || !p->backing) { return false; }
    if (handle.slot >= p->count) { return false; }
    uint8_t *slot_ptr = p->backing->base + (size_t)handle.slot * p->total_slot_size;
    struct veriobjc_gen_slot_header *hdr = (struct veriobjc_gen_slot_header *)slot_ptr;
    return (hdr->generation == handle.generation) && ((hdr->generation & 1) == 1);
}

static inline void *veriobjc_gen_pool_resolve(const struct veriobjc_gen_pool *p,
                                              struct veriobjc_handle handle) {
    if (!veriobjc_gen_pool_is_valid(p, handle)) {
        return NULL;
    }
    uint8_t *slot_ptr = p->backing->base + (size_t)handle.slot * p->total_slot_size;
    return (void *)(slot_ptr + sizeof(struct veriobjc_gen_slot_header));
}

static inline bool veriobjc_gen_pool_free(struct veriobjc_gen_pool *p,
                                         struct veriobjc_handle handle) {
    if (!p || !p->backing) { return false; }
    if (!veriobjc_gen_pool_is_valid(p, handle)) {
        return false; // Stale handle or double free rejected!
    }
    uint8_t *slot_ptr = p->backing->base + (size_t)handle.slot * p->total_slot_size;
    struct veriobjc_gen_slot_header *hdr = (struct veriobjc_gen_slot_header *)slot_ptr;
    hdr->generation++; // Becomes even (free)
    hdr->next_free = p->free_head;
    p->free_head = handle.slot;
    return true;
}


// ---------------------------------------------------------------------------
// Closed-World Object Construction Hook
// ---------------------------------------------------------------------------

static inline id veriobjc_alloc_init(Class cls, struct veriobjc_stack *s) {
    if (!cls || !s) {
        return NULL;
    }
    size_t alloc_size = cls->instance_size;
    if (alloc_size < sizeof(struct veriobjc_object)) {
        alloc_size = sizeof(struct veriobjc_object);
    }
    struct veriobjc_object *obj =
        (struct veriobjc_object *)veriobjc_stack_alloc(s, alloc_size);
    if (obj) {
        obj->isa = cls;
    }
    return (id)obj;
}

#ifdef __cplusplus
}
#endif
