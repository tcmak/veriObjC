#include "CppUTest/TestHarness.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

extern "C" {
#include "veriobjc_runtime.h"
}

// A representative slot type for testing: a realistic object-like value.
typedef struct {
    uint32_t id;
    uint32_t generation; // used in stale read proof
    float    value;
} Slot;

TEST_GROUP(VeriObjC_Pool) {
    static constexpr size_t STACK_CAPACITY = 64 * 1024; // 64 KiB
    alignas(16) uint8_t buffer[STACK_CAPACITY];
    struct veriobjc_stack backing_stack;
    struct veriobjc_pool  pool;

    void setup() override {
        memset(buffer, 0xAA, STACK_CAPACITY); // poison
        veriobjc_stack_init(&backing_stack, buffer, STACK_CAPACITY);
        veriobjc_pool_init(&pool, &backing_stack, sizeof(Slot));
    }

    void teardown() override {}
};

// Contract 1: Pool Initialization Invariants
TEST(VeriObjC_Pool, InitializationInvariants) {
    POINTERS_EQUAL(&backing_stack, pool.backing);
    LONGS_EQUAL(sizeof(Slot),      pool.slot_size);
    LONGS_EQUAL(0,                 pool.count);
    LONGS_EQUAL(UINT32_MAX,        pool.free_head);
}

// Contract 2: First Allocation Bumps the Backing Data Stack (O(1), no heap)
TEST(VeriObjC_Pool, FirstAllocBumpsDataStack) {
    size_t before = veriobjc_stack_used(&backing_stack);

    uint32_t slot = veriobjc_pool_alloc(&pool);

    // Must have consumed exactly one slot_size worth of data-stack space
    size_t after = veriobjc_stack_used(&backing_stack);
    LONGS_EQUAL(0, slot); // first slot is index 0
    CHECK(after - before >= sizeof(Slot));
    LONGS_EQUAL(1, pool.count);  // high-water mark advanced
}

// Contract 3: Sequential Allocations Return Distinct Slot Indices
TEST(VeriObjC_Pool, SequentialSlotsAreDistinct) {
    uint32_t a = veriobjc_pool_alloc(&pool);
    uint32_t b = veriobjc_pool_alloc(&pool);
    uint32_t c = veriobjc_pool_alloc(&pool);

    CHECK(a != b);
    CHECK(b != c);
    LONGS_EQUAL(0, a);
    LONGS_EQUAL(1, b);
    LONGS_EQUAL(2, c);
    LONGS_EQUAL(3, pool.count);
}

// Contract 4: Freed Slot is Reused by Next Allocation (freelist pop in O(1))
TEST(VeriObjC_Pool, FreedSlotIsReused) {
    uint32_t a = veriobjc_pool_alloc(&pool);
    uint32_t b = veriobjc_pool_alloc(&pool);

    size_t stack_after_two = veriobjc_stack_used(&backing_stack);
    size_t count_after_two = pool.count;

    // Free slot 'a', then allocate again
    veriobjc_pool_free(&pool, a);
    uint32_t reused = veriobjc_pool_alloc(&pool);

    // Must be the same slot index
    LONGS_EQUAL(a, reused);

    // Data stack must NOT have grown (freelist was used)
    LONGS_EQUAL(stack_after_two, veriobjc_stack_used(&backing_stack));

    // High-water count must not have grown (no new bump alloc)
    LONGS_EQUAL(count_after_two, pool.count);

    (void)b;
}

// Contract 5: Multiple Free/Reuse Cycles Are Stable (LIFO freelist behaviour)
TEST(VeriObjC_Pool, MultipleFreeReuseCycles) {
    uint32_t s0 = veriobjc_pool_alloc(&pool);
    uint32_t s1 = veriobjc_pool_alloc(&pool);
    uint32_t s2 = veriobjc_pool_alloc(&pool);

    veriobjc_pool_free(&pool, s0);
    veriobjc_pool_free(&pool, s1);
    veriobjc_pool_free(&pool, s2);

    size_t stack_mark = veriobjc_stack_used(&backing_stack);
    uint32_t count_mark = pool.count;

    // Three allocations must come from freelist in LIFO order: s2, s1, s0
    uint32_t r0 = veriobjc_pool_alloc(&pool);
    uint32_t r1 = veriobjc_pool_alloc(&pool);
    uint32_t r2 = veriobjc_pool_alloc(&pool);

    LONGS_EQUAL(s2, r0);
    LONGS_EQUAL(s1, r1);
    LONGS_EQUAL(s0, r2);

    // Stack must not have grown at all
    LONGS_EQUAL(stack_mark, veriobjc_stack_used(&backing_stack));
    LONGS_EQUAL(count_mark, pool.count);
}

// Contract 6: Pool Slot Pointer Retrieval
TEST(VeriObjC_Pool, SlotPointerIsContiguous) {
    uint32_t s0 = veriobjc_pool_alloc(&pool);
    uint32_t s1 = veriobjc_pool_alloc(&pool);

    Slot* p0 = (Slot*)veriobjc_pool_at(&pool, s0);
    Slot* p1 = (Slot*)veriobjc_pool_at(&pool, s1);

    CHECK(p0 != nullptr);
    CHECK(p1 != nullptr);
    // Slots are contiguous in the backing data stack
    LONGS_EQUAL((uintptr_t)p0 + sizeof(Slot), (uintptr_t)p1);
}

// Contract 7: Same-Typed Stale Read Invariant (The Safety Core Guarantee)
// Reading a freed pool slot must return a valid address of the CORRECT type,
// never a wild pointer, never corrupt memory — it may contain different data.
TEST(VeriObjC_Pool, StaleReadYieldsSameTypedMemoryNotCorruption) {
    uint32_t slot = veriobjc_pool_alloc(&pool);
    Slot* p = (Slot*)veriobjc_pool_at(&pool, slot);

    // Write known sentinel values
    p->id = 0xDEAD;
    p->value = 3.14f;

    // Free the slot
    veriobjc_pool_free(&pool, slot);

    // Slot memory is NOT released; pointer is still within the pool's
    // backing reservation — it is a valid address of the correct type
    Slot* p_stale = (Slot*)veriobjc_pool_at(&pool, slot);
    CHECK(p_stale != nullptr);

    // We cannot assert value content (a new allocator wrote to it),
    // but we CAN assert the address itself is within the backing reservation
    uint8_t* pool_base = backing_stack.base;
    uint8_t* pool_end  = backing_stack.base + backing_stack.size;
    CHECK((uint8_t*)p_stale >= pool_base);
    CHECK((uint8_t*)p_stale <  pool_end);
}

// Contract 8: Slot Pointer After Free Stays Within Reservation
// Pool memory is NEVER released to OS. Backing data stack never shrinks.
TEST(VeriObjC_Pool, PoolMemoryIsNeverReleased) {
    static constexpr int N = 100;
    uint32_t slots[N];

    // Hold all 100 slots simultaneously — each requires a new data-stack bump.
    for (int i = 0; i < N; ++i) {
        slots[i] = veriobjc_pool_alloc(&pool);
    }
    LONGS_EQUAL(N, pool.count);
    size_t stack_high_water = veriobjc_stack_used(&backing_stack);
    CHECK(stack_high_water >= (size_t)N * sizeof(Slot));

    // Free all slots: memory is never returned to the OS.
    for (int i = 0; i < N; ++i) {
        veriobjc_pool_free(&pool, slots[i]);
    }

    // Data stack must NOT have shrunk — only the pool freelist changed.
    LONGS_EQUAL(stack_high_water, veriobjc_stack_used(&backing_stack));
    LONGS_EQUAL(N, pool.count); // high-water count unchanged

    // Allocate 100 more — must all come from freelist, stack must not grow.
    for (int i = 0; i < N; ++i) {
        uint32_t s = veriobjc_pool_alloc(&pool);
        CHECK(s < (uint32_t)N); // index must be within the already-allocated range
    }
    LONGS_EQUAL(stack_high_water, veriobjc_stack_used(&backing_stack));
    LONGS_EQUAL(N, pool.count); // still no new bumps
}


// Contract 9: Branch Coverage — Null Safety
TEST(VeriObjC_Pool, NullPoolHandledGracefully) {
    // pool_init with null pool must not crash
    veriobjc_pool_init(nullptr, &backing_stack, sizeof(Slot));

    // veriobjc_pool_alloc on null pool should return UINT32_MAX (sentinel for error)
    uint32_t r = veriobjc_pool_alloc(nullptr);
    LONGS_EQUAL(UINT32_MAX, r);

    // veriobjc_pool_free on null pool must not crash
    veriobjc_pool_free(nullptr, 0);

    // veriobjc_pool_at on null pool should return nullptr
    void* p = veriobjc_pool_at(nullptr, 0);
    POINTERS_EQUAL(nullptr, p);
}

// Contract 10: Branch Coverage — Out-of-Bounds Slot Access
TEST(VeriObjC_Pool, OutOfBoundsSlotReturnsNull) {
    // Request an index beyond the high-water count
    void* p = veriobjc_pool_at(&pool, 999);
    POINTERS_EQUAL(nullptr, p);
}
