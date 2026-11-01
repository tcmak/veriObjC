#include "CppUTest/TestHarness.h"
#include <stdint.h>
#include <stddef.h>

extern "C" {
#include "veriobjc_runtime.h"
}

TEST_GROUP(VeriObjC_DataStack) {
    static constexpr size_t STACK_CAPACITY = 64 * 1024; // 64 KiB
    alignas(16) uint8_t buffer[STACK_CAPACITY];
    struct veriobjc_stack stack;

    void setup() override {
        for (size_t i = 0; i < STACK_CAPACITY; ++i) {
            buffer[i] = 0xAA; // Poison buffer to detect uninitialized writes
        }
        veriobjc_stack_init(&stack, buffer, STACK_CAPACITY);
    }

    void teardown() override {
    }
};

// Contract 1: Deterministic Initialization & State Invariants
TEST(VeriObjC_DataStack, StackInitialization) {
    POINTERS_EQUAL(buffer, stack.base);
    POINTERS_EQUAL(buffer, stack.top);
    LONGS_EQUAL(STACK_CAPACITY, stack.size);
    LONGS_EQUAL(0, veriobjc_stack_used(&stack));
    LONGS_EQUAL(STACK_CAPACITY, veriobjc_stack_available(&stack));
}

// Contract 2: Contiguous Allocation & Alignment Invariants
TEST(VeriObjC_DataStack, SequentialAllocContiguousAndAligned) {
    // Allocate 3 bytes
    void* p1 = veriobjc_stack_alloc(&stack, 3);
    CHECK(p1 != nullptr);
    POINTERS_EQUAL(buffer, p1);

    // Default allocation alignment should align pointers to word boundary (sizeof(void*))
    void* p2 = veriobjc_stack_alloc(&stack, 5);
    CHECK(p2 != nullptr);
    CHECK((uintptr_t)p2 > (uintptr_t)p1);
    CHECK_EQUAL(0, (uintptr_t)p2 % sizeof(void*));

    // Allocate 16 bytes
    void* p3 = veriobjc_stack_alloc(&stack, 16);
    CHECK(p3 != nullptr);
    CHECK((uintptr_t)p3 > (uintptr_t)p2);
    CHECK_EQUAL(0, (uintptr_t)p3 % sizeof(void*));

    CHECK(veriobjc_stack_used(&stack) > 0);
    CHECK(veriobjc_stack_available(&stack) < STACK_CAPACITY);
}

// Contract 3: Capacity Boundaries
TEST(VeriObjC_DataStack, CapacityBoundaries) {
    size_t avail = veriobjc_stack_available(&stack);
    
    // Attempt to allocate more than total capacity
    void* p_overflow = veriobjc_stack_alloc(&stack, avail + 1);
    POINTERS_EQUAL(nullptr, p_overflow);

    // Allocate exact remaining amount
    void* p_exact = veriobjc_stack_alloc(&stack, avail);
    CHECK(p_exact != nullptr);
    LONGS_EQUAL(0, veriobjc_stack_available(&stack));
    LONGS_EQUAL(STACK_CAPACITY, veriobjc_stack_used(&stack));

    // Next allocation must fail
    void* p_none = veriobjc_stack_alloc(&stack, 1);
    POINTERS_EQUAL(nullptr, p_none);
}

// Contract 4: Reference Stability Across Growth (Language Core Guarantee)
TEST(VeriObjC_DataStack, ReferenceStabilityAcrossGrowth) {
    struct Record {
        uint32_t id;
        uint32_t value;
    };

    Record* first = (Record*)veriobjc_stack_alloc(&stack, sizeof(Record));
    CHECK(first != nullptr);
    first->id = 42;
    first->value = 1001;

    uint8_t* captured_addr = (uint8_t*)first;

    // Simulate 1000 subsequent allocations (mimicking push operations on stack)
    for (int i = 0; i < 1000; ++i) {
        uint32_t* next = (uint32_t*)veriobjc_stack_alloc(&stack, sizeof(uint32_t));
        CHECK(next != nullptr);
        *next = (uint32_t)i;
    }

    // Verify first record NEVER moved and was not corrupted
    POINTERS_EQUAL(captured_addr, (uint8_t*)first);
    LONGS_EQUAL(42, first->id);
    LONGS_EQUAL(1001, first->value);

    // Mutate through original reference; must remain safe and valid
    first->value = 9999;
    LONGS_EQUAL(9999, first->value);
}

// Contract 5: One-Store Scope Reset (Zero-Cost Scope Teardown)
TEST(VeriObjC_DataStack, OneStoreScopeReset) {
    // Outer scope mark
    uint8_t* scope_mark = stack.top;

    // Allocate temporary objects inside simulated scope
    for (int i = 0; i < 50; ++i) {
        veriobjc_stack_alloc(&stack, 64);
    }
    CHECK(veriobjc_stack_used(&stack) > 0);

    // Exit scope -> reset to mark
    veriobjc_stack_reset(&stack, scope_mark);

    POINTERS_EQUAL(scope_mark, stack.top);
    LONGS_EQUAL(0, veriobjc_stack_used(&stack));
    LONGS_EQUAL(STACK_CAPACITY, veriobjc_stack_available(&stack));

    // Subsequent allocation reuses the exact same base memory
    void* p_new = veriobjc_stack_alloc(&stack, 128);
    POINTERS_EQUAL(buffer, p_new);
}

// Contract 6: Object Allocation & Layout Invariant (veriobjc_alloc_init)
TEST(VeriObjC_DataStack, ObjectAllocationOnStack) {
    struct veriobjc_class dummy_class = {
        .superclass = nullptr,
        .name = "AudioNode",
        .instance_size = 32
    };

    id obj = veriobjc_alloc_init(&dummy_class, &stack);
    CHECK(obj != nullptr);

    // Verify isa pointer
    POINTERS_EQUAL(&dummy_class, obj->isa);

    // Verify used space is at least instance_size
    CHECK(veriobjc_stack_used(&stack) >= dummy_class.instance_size);
}
