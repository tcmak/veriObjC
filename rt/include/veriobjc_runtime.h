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
struct veriobjc_vtable;

typedef void (*veriobjc_imp_t)(void);

struct veriobjc_vtable_entry {
    uint32_t       sel_id;
    veriobjc_imp_t imp;
};

struct veriobjc_vtable {
    const struct veriobjc_vtable_entry *entries;
    uint32_t mask;
    uint8_t  shift;
};

// Structural root of all objects.
struct veriobjc_object {
    const struct veriobjc_class *isa;
};

struct veriobjc_class {
    const struct veriobjc_class  *superclass;
    const char                   *name;
    size_t                        instance_size;
    const struct veriobjc_vtable *vtable;
};

#ifndef __OBJC__
typedef struct veriobjc_object *id;
#endif
typedef const struct veriobjc_class *Class;
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
// Scoped Cleanup Hook (VeriObjC_ScopeRelease / @scoped)
//
// Leverages GCC/Clang __attribute__((cleanup)) to emit automatic single-store
// resets of data-stack marks upon lexical block exit.
// ---------------------------------------------------------------------------

struct veriobjc_scope_guard {
    struct veriobjc_stack *stack;
    uint8_t *mark;
};

static inline void veriobjc_scope_cleanup(struct veriobjc_scope_guard *g) {
    if (g && g->stack) {
        veriobjc_stack_reset(g->stack, g->mark);
    }
}

static inline void VeriObjC_ScopeRelease(void *guard_ptr) {
    veriobjc_scope_cleanup((struct veriobjc_scope_guard *)guard_ptr);
}

#define VERIOBJC_CONCAT_INNER(a, b) a ## b
#define VERIOBJC_CONCAT(a, b) VERIOBJC_CONCAT_INNER(a, b)

#define VERIOBJC_SCOPED_STACK(s_ptr) \
    __attribute__((cleanup(veriobjc_scope_cleanup))) \
    struct veriobjc_scope_guard VERIOBJC_CONCAT(_v_guard_, __LINE__) = { \
        .stack = (s_ptr), \
        .mark = (s_ptr) ? (s_ptr)->top : NULL \
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

static inline void veriobjc_pool_init(struct veriobjc_pool *p, struct veriobjc_stack *s, size_t slot_size) {
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

static inline void veriobjc_gen_pool_init(struct veriobjc_gen_pool *p, struct veriobjc_stack *s, size_t payload_size) {
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

static inline bool veriobjc_gen_pool_is_valid(const struct veriobjc_gen_pool *p, struct veriobjc_handle handle) {
    if (!p || !p->backing) { return false; }
    if (handle.slot >= p->count) { return false; }
    uint8_t *slot_ptr = p->backing->base + (size_t)handle.slot * p->total_slot_size;
    struct veriobjc_gen_slot_header *hdr = (struct veriobjc_gen_slot_header *)slot_ptr;
    return (hdr->generation == handle.generation) && ((hdr->generation & 1) == 1);
}

static inline void *veriobjc_gen_pool_resolve(const struct veriobjc_gen_pool *p, struct veriobjc_handle handle) {
    if (!veriobjc_gen_pool_is_valid(p, handle)) {
        return NULL;
    }
    uint8_t *slot_ptr = p->backing->base + (size_t)handle.slot * p->total_slot_size;
    return (void *)(slot_ptr + sizeof(struct veriobjc_gen_slot_header));
}

static inline bool veriobjc_gen_pool_free(struct veriobjc_gen_pool *p, struct veriobjc_handle handle) {
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
// Link-Time Perfect-Hashed Jump Table & Selector Hashing
// ---------------------------------------------------------------------------

static inline uint32_t veriobjc_sel_hash(const char *sel_name) {
    if (!sel_name) {
        return 0u;
    }
    uint32_t hash = 0x811c9dc5u; // FNV-1a 32-bit offset basis
    while (*sel_name) {
        hash ^= (uint32_t)(uint8_t)(*sel_name++);
        hash *= 0x01000193u;     // FNV-1a 32-bit prime
    }
    return hash;
}

static inline veriobjc_imp_t veriobjc_dispatch_lookup(const struct veriobjc_class *cls, uint32_t sel_id) {
    if (sel_id == 0u) {
        return NULL;
    }
    for (const struct veriobjc_class *cur = cls; cur != NULL; cur = cur->superclass) {
        const struct veriobjc_vtable *vt = cur->vtable;
        if (vt && vt->entries) {
            uint32_t idx = (sel_id >> vt->shift) & vt->mask;
            const struct veriobjc_vtable_entry *entry = &vt->entries[idx];
            if (entry->sel_id == sel_id) {
                return entry->imp;
            }
        }
    }
    return NULL;
}

static inline veriobjc_imp_t veriobjc_object_dispatch(const struct veriobjc_object *obj, uint32_t sel_id) {
    if (!obj) {
        return NULL;
    }
    return veriobjc_dispatch_lookup(obj->isa, sel_id);
}

// ---------------------------------------------------------------------------
// Linear Ownership & Move Semantics Intrinsic
// ---------------------------------------------------------------------------

static inline void __veriobjc_mark_moved(const void *ptr) {
    (void)ptr;
}

#define veriobjc_move(x) (__veriobjc_mark_moved(&(x)), (x))

// ---------------------------------------------------------------------------
// Multi-Frame Error Return Intrinsic (return err from f)
// ---------------------------------------------------------------------------

static inline int32_t __veriobjc_return_err_from(int32_t err_code, const char *target_frame) {
    (void)target_frame;
    return err_code;
}

#define veriobjc_return_err_from(err_code, target_frame) \
    return __veriobjc_return_err_from((err_code), #target_frame)

// ---------------------------------------------------------------------------
// Fueled Compile-Time Evaluation (vconstexpr)
// ---------------------------------------------------------------------------

#ifndef vconstexpr
#define vconstexpr __attribute__((objc_vconstexpr)) static inline
#endif

// ---------------------------------------------------------------------------
// Compile-Time Regex (vregex) DFA Table & Fixed-Trip Matcher
// ---------------------------------------------------------------------------

struct veriobjc_dfa {
    const uint8_t *transitions;
    const uint8_t *accept_mask;
    uint32_t       num_states;
    uint32_t       start_state;
};

static inline bool veriobjc_dfa_match(const struct veriobjc_dfa *dfa, const char *input, size_t len) {
    if (!dfa || !dfa->transitions || !dfa->accept_mask || dfa->num_states == 0u || (!input && len > 0u)) {
        return false;
    }
    uint32_t state = dfa->start_state;
    for (size_t i = 0; i < len; ++i) {
        uint8_t byte = (uint8_t)input[i];
        state = (uint32_t)dfa->transitions[state * 256u + (uint32_t)byte];
    }
    return dfa->accept_mask[state] != 0u;
}

static inline bool __veriobjc_regex_match(const char *pattern, const char *input, size_t len) {
    (void)pattern;
    (void)input;
    (void)len;
    return false;
}

#define vregex_match(pattern_literal, input_ptr, input_len) \
    __veriobjc_regex_match((pattern_literal), (input_ptr), (input_len))

// ---------------------------------------------------------------------------
// Nominal Local Type Inference (auto / vauto) & Strong Types (vstrong)
// ---------------------------------------------------------------------------

typedef struct __veriobjc_inferred_auto {
    int64_t __opaque;
} vauto;

#define veriobjc_export               __attribute__((objc_export))
#define veriobjc_nolatency            __attribute__((objc_nolatency))
#define veriobjc_pure                 __attribute__((objc_pure))
#define veriobjc_cold                 __attribute__((objc_cold))
#define veriobjc_effect_alloc         __attribute__((objc_effect_alloc))
#define veriobjc_effect_io            __attribute__((objc_effect_io))
#define veriobjc_effect_blocking      __attribute__((objc_effect_blocking))
#define veriobjc_effect_fence         __attribute__((objc_effect_fence))
#define veriobjc_effect_shared_retain __attribute__((objc_effect_shared_retain))
#define veriobjc_pre(cond_str)        __attribute__((annotate("objc_pre:" cond_str)))
#define veriobjc_post(cond_str)       __attribute__((annotate("objc_post:" cond_str)))
#define veriobjc_packed               __attribute__((packed))
#define veriobjc_aligned(n)           __attribute__((aligned(n)))
#define vstatic_assert(cond, msg)     _Static_assert(cond, msg)
#define veriobjc_specialize           __attribute__((objc_specialize))
#define vauto_borrow(var, expr) auto var = &(expr)
#define vstrong_def(Name, UnderlyingType) \
    typedef struct { UnderlyingType raw; } Name

// ---------------------------------------------------------------------------
// Compile-Time Reflection (@reflect), SoA Transmuter (objc_soa),
// vvariant / vmatch, vtrait, Monomorphization Budgets, Static Topology &
// Fixed Task DAGs (vasync), and Built-in-Place Construction (Section 2.8)
// ---------------------------------------------------------------------------

#define veriobjc_soa          __attribute__((objc_soa, objc_flat, aligned(64)))
#define veriobjc_vvariant     __attribute__((objc_vvariant, objc_flat))
#define veriobjc_inplace_only __attribute__((objc_inplace_only, objc_flat))
#define vmatch(variant_expr)  switch ((variant_expr).tag)
#define vtrait                __attribute__((objc_vconstexpr, objc_vtrait)) static inline

#define vsoa_def3(SoAName, Capacity, T1, F1, T2, F2, T3, F3) \
    struct veriobjc_soa SoAName { \
        T1 F1[Capacity]; \
        T2 F2[Capacity]; \
        T3 F3[Capacity]; \
    }

static inline uint32_t __veriobjc_reflect_field_count(const void *type_tag) {
    (void)type_tag;
    return 0u;
}

#define veriobjc_reflect_field_count(Type) \
    __veriobjc_reflect_field_count((const Type *)0)

#define veriobjc_reflect_sizeof(Type) \
    ((uint32_t)sizeof(Type))

#define veriobjc_reflect_offset(Type, Field) \
    __builtin_offsetof(Type, Field)

static inline void __veriobjc_mono_instantiate(const char *pkg_name, const char *generic_name, uint32_t code_bytes, uint32_t max_budget) {
    (void)pkg_name;
    (void)generic_name;
    (void)code_bytes;
    (void)max_budget;
}

#define vmono_instantiate(pkg, generic_sig, code_bytes, max_budget) \
    __veriobjc_mono_instantiate((pkg), (generic_sig), (uint32_t)(code_bytes), (uint32_t)(max_budget))

struct veriobjc_core_topology {
    uint16_t core_id;
    uint16_t numa_node;
};

#define veriobjc_core_topology_def(Name, CoreId, NumaNode) \
    _Static_assert((CoreId) < 256, "VeriObjC topology core_id out of bounds"); \
    _Static_assert((NumaNode) < 16, "VeriObjC topology numa_node out of bounds"); \
    static const struct veriobjc_core_topology Name = { .core_id = (uint16_t)(CoreId), .numa_node = (uint16_t)(NumaNode) }

static inline void __veriobjc_dag_edge(const char *from_node, const char *to_node, uint32_t wcet_cycles) {
    (void)from_node;
    (void)to_node;
    (void)wcet_cycles;
}

static inline uint32_t __veriobjc_dag_critical_path_wcet(void) {
    return 0u;
}

#define veriobjc_dag_edge(from_node, to_node, wcet_cycles) \
    __veriobjc_dag_edge((from_node), (to_node), (uint32_t)(wcet_cycles))

#define veriobjc_dag_critical_path_wcet() \
    __veriobjc_dag_critical_path_wcet()

enum veriobjc_vasync_status {
    VERIOBJC_VASYNC_PENDING = 0,
    VERIOBJC_VASYNC_READY   = 1
};

struct veriobjc_vasync_frame {
    uint16_t core_id;
    uint16_t status;
    uint32_t result_code;
};

static inline void veriobjc_vasync_init(struct veriobjc_vasync_frame *frame, uint16_t core_id) {
    if (!frame) { return; }
    frame->core_id     = core_id;
    frame->status      = (uint16_t)VERIOBJC_VASYNC_PENDING;
    frame->result_code = 0u;
}

static inline void veriobjc_vasync_wake_irq(struct veriobjc_vasync_frame *frame, uint32_t result_code) {
    if (!frame) { return; }
    frame->result_code = result_code;
    frame->status      = (uint16_t)VERIOBJC_VASYNC_READY;
}

static inline bool veriobjc_vasync_is_ready(const struct veriobjc_vasync_frame *frame) {
    if (!frame) { return false; }
    return frame->status == (uint16_t)VERIOBJC_VASYNC_READY;
}

#define veriobjc_push_inplace(array_base, count_lval, cap, init_fn, ...) \
    do { \
        if ((count_lval) < (cap)) { \
            init_fn(&(array_base)[(count_lval)], __VA_ARGS__); \
            (count_lval)++; \
        } \
    } while (0)

// ---------------------------------------------------------------------------
// Hardware Stream Elaboration Substrate (veriStream: Bits<W>, Stream<T, N>)
// ---------------------------------------------------------------------------

#define veriobjc_hw_kernel __attribute__((objc_hw_kernel))

#define vstream_def(Name, ElemType, WidthN) \
    __attribute__((objc_flat)) \
    typedef struct { ElemType lanes[WidthN]; } Name

__attribute__((annotate("objc_vconstexpr"))) static inline uint32_t veriobjc_clog2(uint32_t n) {
    uint32_t stages = 0u;
    uint32_t p = 1u;
    while (p < n) {
        p = p << 1u;
        stages = stages + 1u;
    }
    return stages;
}

static inline int32_t veriobjc_cmux(int32_t cond_nonzero, int32_t val_true, int32_t val_false) {
    int32_t mask = -(cond_nonzero != 0);
    return (val_true & mask) | (val_false & ~mask);
}

static inline int32_t veriobjc_ternary_select(int8_t weight_trit, int8_t activation) {
    int32_t x = (int32_t)activation;
    int32_t neg_x = -x;
    int32_t sel_neg = veriobjc_cmux(weight_trit == -1, neg_x, 0);
    return veriobjc_cmux(weight_trit == 1, x, sel_neg);
}

static inline int32_t veriobjc_ternary_popcount_reduce(uint64_t pos_mask, uint64_t neg_mask) {
    return __builtin_popcountll(pos_mask) - __builtin_popcountll(neg_mask);
}

static inline int32_t veriobjc_ternary_dot_512(const int8_t *activations, const uint8_t *packed_weights_128) {
    if (!activations || !packed_weights_128) {
        return 0;
    }
    int32_t acc = 0;
    for (uint32_t byte_idx = 0u; byte_idx < 128u; ++byte_idx) {
        uint8_t packed = packed_weights_128[byte_idx];
        for (uint32_t sub = 0u; sub < 4u; ++sub) {
            uint8_t code = (uint8_t)((packed >> (sub << 1u)) & 0x3u);
            int8_t trit = (int8_t)veriobjc_cmux(code == 1u, 1, veriobjc_cmux(code == 2u, -1, 0));
            acc += veriobjc_ternary_select(trit, activations[(byte_idx << 2u) + sub]);
        }
    }
    return acc;
}

static inline int32_t veriobjc_glu_gate(int32_t gate_val, int32_t up_val) {
    return veriobjc_cmux(gate_val > 0, up_val, 0);
}

static inline void __veriobjc_stream_zip(const void *s1, uint32_t n1, const void *s2, uint32_t n2) {
    (void)s1;
    (void)n1;
    (void)s2;
    (void)n2;
}

static inline void __veriobjc_check_context_cap(uint32_t requested_tokens, uint32_t context_cap) {
    (void)requested_tokens;
    (void)context_cap;
}

static inline void __veriobjc_verify_reduce_latency(uint32_t width_n, uint32_t declared_cycles) {
    (void)width_n;
    (void)declared_cycles;
}

#define vstream_zip(s1, s2) \
    __veriobjc_stream_zip(&(s1), (uint32_t)(sizeof((s1).lanes) / sizeof((s1).lanes[0])), \
                          &(s2), (uint32_t)(sizeof((s2).lanes) / sizeof((s2).lanes[0])))

#define vstream_check_context(requested, cap) \
    __veriobjc_check_context_cap((uint32_t)(requested), (uint32_t)(cap))

#define vstream_latency_contract(width_n, cycles) \
    __veriobjc_verify_reduce_latency((uint32_t)(width_n), (uint32_t)(cycles))

// ---------------------------------------------------------------------------
// Lock-Free Single-Producer Single-Consumer Flat-Value Channel Stub
// ---------------------------------------------------------------------------

struct veriobjc_channel {
    uint8_t *ring_base;
    size_t   slot_size;
    uint32_t capacity;
    uint32_t head;
    uint32_t tail;
};

static inline void veriobjc_channel_init(struct veriobjc_channel *ch, void *ring_buf, size_t slot_size, uint32_t capacity) {
    if (!ch) { return; }
    ch->ring_base = (uint8_t *)ring_buf;
    ch->slot_size = slot_size;
    ch->capacity  = capacity;
    ch->head      = 0u;
    ch->tail      = 0u;
}

static inline bool veriobjc_channel_send(struct veriobjc_channel *ch, const void *flat_payload, size_t payload_size) {
    if (!ch || !ch->ring_base || !flat_payload || payload_size != ch->slot_size || ch->capacity == 0u) {
        return false;
    }
    if (ch->head - ch->tail >= ch->capacity) {
        return false;
    }
    uint32_t slot = ch->head % ch->capacity;
    __builtin_memcpy(ch->ring_base + (size_t)slot * ch->slot_size, flat_payload, payload_size);
    ch->head++;
    return true;
}

static inline bool veriobjc_channel_recv(struct veriobjc_channel *ch, void *out_flat_payload, size_t payload_size) {
    if (!ch || !ch->ring_base || !out_flat_payload || payload_size != ch->slot_size || ch->head == ch->tail) {
        return false;
    }
    uint32_t slot = ch->tail % ch->capacity;
    __builtin_memcpy(out_flat_payload, ch->ring_base + (size_t)slot * ch->slot_size, payload_size);
    ch->tail++;
    return true;
}

// ---------------------------------------------------------------------------
// Closed-World Object Construction Hook
// ---------------------------------------------------------------------------

static inline struct veriobjc_object *veriobjc_alloc_init(Class cls, struct veriobjc_stack *s) {
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
    return obj;
}

// ---------------------------------------------------------------------------
// Self-Relative Link Primitives (__relative(8|16|32))
//
// Narrow signed offset pointers relative to the link field's own address.
// An offset of 0 encodes NULL; structures using self-relative links are
// position-independent across memory, persistence, and wire buffers.
// ---------------------------------------------------------------------------

struct veriobjc_rel8  { int8_t  offset; };
struct veriobjc_rel16 { int16_t offset; };
struct veriobjc_rel32 { int32_t offset; };

#ifndef __relative
#define __relative(bits) struct VERIOBJC_CONCAT(veriobjc_rel, bits)
#endif

static inline bool veriobjc_rel8_set(struct veriobjc_rel8 *link, const void *target) {
    if (!link) {
        return false;
    }
    if (!target) {
        link->offset = 0;
        return true;
    }
    ptrdiff_t delta = (const uint8_t *)target - (const uint8_t *)link;
    if (delta == 0 || delta < INT8_MIN || delta > INT8_MAX) {
        link->offset = 0;
        return false;
    }
    link->offset = (int8_t)delta;
    return true;
}

static inline void *veriobjc_rel8_get(const struct veriobjc_rel8 *link) {
    if (!link || link->offset == 0) {
        return NULL;
    }
    return (void *)((const uint8_t *)link + link->offset);
}

static inline bool veriobjc_rel16_set(struct veriobjc_rel16 *link, const void *target) {
    if (!link) {
        return false;
    }
    if (!target) {
        link->offset = 0;
        return true;
    }
    ptrdiff_t delta = (const uint8_t *)target - (const uint8_t *)link;
    if (delta == 0 || delta < INT16_MIN || delta > INT16_MAX) {
        link->offset = 0;
        return false;
    }
    link->offset = (int16_t)delta;
    return true;
}

static inline void *veriobjc_rel16_get(const struct veriobjc_rel16 *link) {
    if (!link || link->offset == 0) {
        return NULL;
    }
    return (void *)((const uint8_t *)link + link->offset);
}

static inline bool veriobjc_rel32_set(struct veriobjc_rel32 *link, const void *target) {
    if (!link) {
        return false;
    }
    if (!target) {
        link->offset = 0;
        return true;
    }
    ptrdiff_t delta = (const uint8_t *)target - (const uint8_t *)link;
    if (delta == 0 || delta < INT32_MIN || delta > INT32_MAX) {
        link->offset = 0;
        return false;
    }
    link->offset = (int32_t)delta;
    return true;
}

static inline void *veriobjc_rel32_get(const struct veriobjc_rel32 *link) {
    if (!link || link->offset == 0) {
        return NULL;
    }
    return (void *)((const uint8_t *)link + link->offset);
}

// ---------------------------------------------------------------------------
// Self-Relative Tape & Snapshot Framing (to_bytes / from_bytes)
// ---------------------------------------------------------------------------

#define VERIOBJC_TAPE_MAGIC   0x564F424Au // "VOBJ"
#define VERIOBJC_TAPE_VERSION 1u

struct veriobjc_tape_header {
    uint32_t magic;
    uint16_t version;
    uint16_t type_tag;
    uint32_t elem_size;
    uint32_t elem_count;
    uint32_t payload_bytes;
    uint32_t checksum;
};

static inline uint32_t veriobjc_tape_checksum(const struct veriobjc_tape_header *hdr, const uint8_t *payload, size_t payload_bytes) {
    uint32_t hash = 0x811c9dc5u;
    const uint8_t *hdr_bytes = (const uint8_t *)hdr;
    size_t hdr_prefix = offsetof(struct veriobjc_tape_header, checksum);
    for (size_t i = 0; i < hdr_prefix; ++i) {
        hash ^= (uint32_t)hdr_bytes[i];
        hash *= 0x01000193u;
    }
    for (size_t i = 0; i < payload_bytes; ++i) {
        hash ^= (uint32_t)payload[i];
        hash *= 0x01000193u;
    }
    return hash;
}

static inline size_t veriobjc_to_bytes(const void *payload, uint32_t elem_size, uint32_t elem_count, uint16_t type_tag, void *out_buf, size_t out_capacity) {
    if (!out_buf || elem_size == 0u) {
        return 0u;
    }
    if (elem_count > 0u && !payload) {
        return 0u;
    }
    uint64_t total_payload = (uint64_t)elem_size * (uint64_t)elem_count;
    if (total_payload > UINT32_MAX - sizeof(struct veriobjc_tape_header)) {
        return 0u;
    }
    size_t total_frame = sizeof(struct veriobjc_tape_header) + (size_t)total_payload;
    if (out_capacity < total_frame) {
        return 0u;
    }

    struct veriobjc_tape_header hdr = {
        .magic         = VERIOBJC_TAPE_MAGIC,
        .version       = VERIOBJC_TAPE_VERSION,
        .type_tag      = type_tag,
        .elem_size     = elem_size,
        .elem_count    = elem_count,
        .payload_bytes = (uint32_t)total_payload,
        .checksum      = 0u
    };
    hdr.checksum = veriobjc_tape_checksum(&hdr, (const uint8_t *)payload, (size_t)total_payload);

    uint8_t *dst = (uint8_t *)out_buf;
    __builtin_memcpy(dst, &hdr, sizeof(hdr));
    if (total_payload > 0u) {
        __builtin_memcpy(dst + sizeof(hdr), payload, (size_t)total_payload);
    }
    return total_frame;
}

static inline const void *veriobjc_from_bytes(const void *in_buf, size_t in_len, uint16_t expected_tag, uint32_t expected_elem_size, uint32_t *out_elem_count) {
    if (!in_buf || expected_elem_size == 0u || in_len < sizeof(struct veriobjc_tape_header)) {
        return NULL;
    }

    struct veriobjc_tape_header hdr;
    __builtin_memcpy(&hdr, in_buf, sizeof(hdr));

    if (hdr.magic != VERIOBJC_TAPE_MAGIC ||
        hdr.version != VERIOBJC_TAPE_VERSION ||
        hdr.type_tag != expected_tag ||
        hdr.elem_size != expected_elem_size) {
        return NULL;
    }

    uint64_t expected_payload = (uint64_t)hdr.elem_size * (uint64_t)hdr.elem_count;
    if (expected_payload != (uint64_t)hdr.payload_bytes) {
        return NULL;
    }

    if (in_len - sizeof(struct veriobjc_tape_header) != (size_t)hdr.payload_bytes) {
        return NULL;
    }

    const uint8_t *payload = (const uint8_t *)in_buf + sizeof(struct veriobjc_tape_header);
    uint32_t computed_crc = veriobjc_tape_checksum(&hdr, payload, (size_t)hdr.payload_bytes);
    if (computed_crc != hdr.checksum) {
        return NULL;
    }

    if (out_elem_count) {
        *out_elem_count = hdr.elem_count;
    }
    return (const void *)payload;
}

static inline bool veriobjc_verify_rel32_link(const struct veriobjc_rel32 *link, const void *payload_base, size_t payload_bytes, size_t elem_size) {
    if (!link || !payload_base || elem_size == 0u || payload_bytes < elem_size) {
        return false;
    }

    const uint8_t *base = (const uint8_t *)payload_base;
    const uint8_t *end  = base + payload_bytes;
    const uint8_t *link_addr = (const uint8_t *)link;

    if (link_addr < base || link_addr > end - sizeof(struct veriobjc_rel32)) {
        return false;
    }

    if (link->offset == 0) {
        return true;
    }

    ptrdiff_t link_pos = link_addr - base;
    ptrdiff_t target_pos = link_pos + (ptrdiff_t)link->offset;
    if (target_pos < 0 || (size_t)target_pos > payload_bytes - elem_size) {
        return false;
    }

    if (((size_t)target_pos % elem_size) != 0u) {
        return false;
    }

    return true;
}

#ifdef __cplusplus
}
#endif
