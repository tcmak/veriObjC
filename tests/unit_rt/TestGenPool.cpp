#include "CppUTest/TestHarness.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

extern "C" {
#include "veriobjc_runtime.h"
}

typedef struct {
    uint32_t id;
    float    value;
} Item;

TEST_GROUP(VeriObjC_GenPool) {
    static constexpr size_t STACK_CAPACITY = 64 * 1024; // 64 KiB
    alignas(16) uint8_t buffer[STACK_CAPACITY];
    struct veriobjc_stack backing_stack;
    struct veriobjc_gen_pool pool;

    void setup() override {
        memset(buffer, 0xAA, STACK_CAPACITY);
        veriobjc_stack_init(&backing_stack, buffer, STACK_CAPACITY);
        veriobjc_gen_pool_init(&pool, &backing_stack, sizeof(Item));
    }

    void teardown() override {}
};

// Contract 1: Generation Pool Initialization Invariants
TEST(VeriObjC_GenPool, InitializationInvariants) {
    POINTERS_EQUAL(&backing_stack, pool.backing);
    LONGS_EQUAL(sizeof(Item), pool.payload_size);
    LONGS_EQUAL(0, pool.count);
    LONGS_EQUAL(UINT32_MAX, pool.free_head);
}

// Contract 2: Alloc returns handle with initial odd generation (active)
TEST(VeriObjC_GenPool, AllocReturnsValidHandle) {
    struct veriobjc_handle h0 = veriobjc_gen_pool_alloc(&pool);
    LONGS_EQUAL(0, h0.slot);
    LONGS_EQUAL(1, h0.generation); // First active generation is 1
    CHECK(veriobjc_gen_pool_is_valid(&pool, h0));

    Item* item = (Item*)veriobjc_gen_pool_resolve(&pool, h0);
    CHECK(item != nullptr);
    item->id = 42;
    item->value = 99.5f;

    // Verify values remain intact when resolved again
    Item* item2 = (Item*)veriobjc_gen_pool_resolve(&pool, h0);
    POINTERS_EQUAL(item, item2);
    LONGS_EQUAL(42, item2->id);
}

// Contract 3: Free invalidates the handle immediately (stale read detection)
TEST(VeriObjC_GenPool, FreeInvalidatesHandleImmediately) {
    struct veriobjc_handle h = veriobjc_gen_pool_alloc(&pool);
    CHECK(veriobjc_gen_pool_is_valid(&pool, h));

    bool freed = veriobjc_gen_pool_free(&pool, h);
    CHECK_TRUE(freed);

    // Stale check: is_valid must now return false
    CHECK_FALSE(veriobjc_gen_pool_is_valid(&pool, h));

    // Stale resolve: must return nullptr instead of dangling pointer
    Item* stale_ptr = (Item*)veriobjc_gen_pool_resolve(&pool, h);
    POINTERS_EQUAL(nullptr, stale_ptr);
}

// Contract 4: Stale Handle Detection After Slot Reuse
// When slot is reallocated, generation is bumped again. The OLD handle must still be reported stale!
TEST(VeriObjC_GenPool, StaleHandleDetectedAfterSlotReuse) {
    struct veriobjc_handle h_old = veriobjc_gen_pool_alloc(&pool);
    LONGS_EQUAL(0, h_old.slot);
    LONGS_EQUAL(1, h_old.generation);

    Item* p_old = (Item*)veriobjc_gen_pool_resolve(&pool, h_old);
    p_old->id = 100;

    // Free the slot (generation bumps from 1 to 2)
    CHECK_TRUE(veriobjc_gen_pool_free(&pool, h_old));

    // Next allocation reuses slot 0 (generation bumps from 2 to 3)
    struct veriobjc_handle h_new = veriobjc_gen_pool_alloc(&pool);
    LONGS_EQUAL(0, h_new.slot);
    LONGS_EQUAL(3, h_new.generation);

    Item* p_new = (Item*)veriobjc_gen_pool_resolve(&pool, h_new);
    CHECK(p_new != nullptr);
    p_new->id = 200; // New owner writes to slot

    // Crucial check: old handle h_old points to same slot 0, but its generation (1) != current (3)
    CHECK_FALSE(veriobjc_gen_pool_is_valid(&pool, h_old));
    POINTERS_EQUAL(nullptr, veriobjc_gen_pool_resolve(&pool, h_old));

    // New handle h_new is completely valid
    CHECK_TRUE(veriobjc_gen_pool_is_valid(&pool, h_new));
    LONGS_EQUAL(200, p_new->id);
}

// Contract 5: Double-Free Protection
TEST(VeriObjC_GenPool, DoubleFreeIsSafelyRejected) {
    struct veriobjc_handle h = veriobjc_gen_pool_alloc(&pool);
    CHECK_TRUE(veriobjc_gen_pool_free(&pool, h));

    // Attempting to free the same handle again must fail
    bool second_free = veriobjc_gen_pool_free(&pool, h);
    CHECK_FALSE(second_free);
}

// Contract 6: Null and Out-of-Bounds Defensive Checks (100% Branch Coverage)
TEST(VeriObjC_GenPool, NullAndOutOfBoundsGuards) {
    struct veriobjc_handle dummy = { .slot = 0, .generation = 1 };

    // Null pool tests
    veriobjc_gen_pool_init(nullptr, &backing_stack, sizeof(Item));

    struct veriobjc_handle h_null = veriobjc_gen_pool_alloc(nullptr);
    LONGS_EQUAL(UINT32_MAX, h_null.slot);
    LONGS_EQUAL(0, h_null.generation);

    CHECK_FALSE(veriobjc_gen_pool_free(nullptr, dummy));
    CHECK_FALSE(veriobjc_gen_pool_is_valid(nullptr, dummy));
    POINTERS_EQUAL(nullptr, veriobjc_gen_pool_resolve(nullptr, dummy));

    // Pool with null backing stack
    struct veriobjc_gen_pool pool_no_backing;
    veriobjc_gen_pool_init(&pool_no_backing, nullptr, sizeof(Item));
    struct veriobjc_handle h_noback = veriobjc_gen_pool_alloc(&pool_no_backing);
    LONGS_EQUAL(UINT32_MAX, h_noback.slot);
    CHECK_FALSE(veriobjc_gen_pool_is_valid(&pool_no_backing, dummy));
    CHECK_FALSE(veriobjc_gen_pool_free(&pool_no_backing, dummy));

    // Out-of-bounds slot
    struct veriobjc_handle h_oob = { .slot = 999, .generation = 1 };
    CHECK_FALSE(veriobjc_gen_pool_is_valid(&pool, h_oob));
    CHECK_FALSE(veriobjc_gen_pool_free(&pool, h_oob));
    POINTERS_EQUAL(nullptr, veriobjc_gen_pool_resolve(&pool, h_oob));

    // Forged even generation handle (points to a freed/inactive slot)
    struct veriobjc_handle h_even = veriobjc_gen_pool_alloc(&pool);
    CHECK_TRUE(veriobjc_gen_pool_free(&pool, h_even));
    // Now slot 0 has generation 2 (even). If someone queries with generation 2:
    struct veriobjc_handle h_forged = { .slot = h_even.slot, .generation = 2 };
    CHECK_FALSE(veriobjc_gen_pool_is_valid(&pool, h_forged));
}

// Contract 7: GenPool Stack Exhaustion
TEST(VeriObjC_GenPool, AllocFailsGracefullyWhenStackFull) {
    // Fill backing stack completely
    size_t avail = veriobjc_stack_available(&backing_stack);
    veriobjc_stack_alloc(&backing_stack, avail);
    LONGS_EQUAL(0, veriobjc_stack_available(&backing_stack));

    // Alloc must return invalid handle
    struct veriobjc_handle h = veriobjc_gen_pool_alloc(&pool);
    LONGS_EQUAL(UINT32_MAX, h.slot);
    LONGS_EQUAL(0, h.generation);
    CHECK_FALSE(veriobjc_gen_pool_is_valid(&pool, h));
}

