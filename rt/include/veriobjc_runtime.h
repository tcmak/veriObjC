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
    size_t aligned_n = veriobjc_align_up(n, VERIOBJC_DEFAULT_ALIGN);
    if ((size_t)(s->top - s->base) + aligned_n > s->size) {
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
