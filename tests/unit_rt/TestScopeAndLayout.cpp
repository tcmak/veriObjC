#include "CppUTest/TestHarness.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

extern "C" {
#include "veriobjc_runtime.h"
}

// Contract 2: Headerless @owned struct layout (Zero Base Tax)
typedef struct {
    float    x;
    float    y;
    uint32_t id;
} HeaderlessParticle;

// Polymorphic object layout with isa pointer
typedef struct {
    struct veriobjc_object header;
    float    amplitude;
    uint32_t channel;
} PolymorphicAudioNode;

TEST_GROUP(VeriObjC_ScopeAndLayout) {
    static constexpr size_t STACK_CAPACITY = 64 * 1024; // 64 KiB
    alignas(16) uint8_t buffer[STACK_CAPACITY];
    struct veriobjc_stack stack;

    void setup() override {
        memset(buffer, 0xAA, STACK_CAPACITY);
        veriobjc_stack_init(&stack, buffer, STACK_CAPACITY);
    }

    void teardown() override {}
};

// Contract 1: Headerless @owned structs have zero base tax
TEST(VeriObjC_ScopeAndLayout, HeaderlessStructHasZeroBaseTax) {
    // Exact sum of fields: 4 + 4 + 4 = 12 bytes
    size_t expected_fields_size = sizeof(float) + sizeof(float) + sizeof(uint32_t);
    LONGS_EQUAL(12, sizeof(HeaderlessParticle));
    LONGS_EQUAL(expected_fields_size, sizeof(HeaderlessParticle));

    // When allocated on data stack, takes exact aligned size without hidden prefix
    void* p = veriobjc_stack_alloc(&stack, sizeof(HeaderlessParticle));
    CHECK(p != nullptr);
    POINTERS_EQUAL(buffer, p);
}

// Contract 2: Polymorphic structs have only isa header (sizeof(void*))
TEST(VeriObjC_ScopeAndLayout, PolymorphicObjectHasOnlyIsaPrefix) {
    size_t base_header_size = sizeof(struct veriobjc_object);
    LONGS_EQUAL(sizeof(void*), base_header_size);

    size_t expected_total = sizeof(void*) + sizeof(float) + sizeof(uint32_t);
    LONGS_EQUAL(expected_total, sizeof(PolymorphicAudioNode));
}

// Contract 3: Automatic Scope Cleanup Hook (__attribute__((cleanup)))
static void simulate_inner_scope(struct veriobjc_stack *s, size_t alloc_amount, uint8_t **out_mark) {
    VERIOBJC_SCOPED_STACK(s); // Creates RAII scope guard with __attribute__((cleanup))
    *out_mark = s->top;

    // Allocate memory inside this lexical scope
    veriobjc_stack_alloc(s, alloc_amount);
    CHECK(veriobjc_stack_used(s) >= alloc_amount);

    // On exit from this function, cleanup hook must fire and restore top to *out_mark
}

TEST(VeriObjC_ScopeAndLayout, ScopedStackCleanupAutomatesReset) {
    uint8_t *initial_top = stack.top;
    uint8_t *scope_mark = nullptr;

    simulate_inner_scope(&stack, 512, &scope_mark);

    // After exiting inner scope, stack top must have automatically reverted to initial_top
    POINTERS_EQUAL(initial_top, stack.top);
    LONGS_EQUAL(0, veriobjc_stack_used(&stack));
    LONGS_EQUAL(STACK_CAPACITY, veriobjc_stack_available(&stack));
}

// Contract 4: Nested Scopes Unwind Deterministically (LIFO)
static void simulate_nested_scopes(struct veriobjc_stack *s) {
    VERIOBJC_SCOPED_STACK(s);
    veriobjc_stack_alloc(s, 128);
    size_t level1_used = veriobjc_stack_used(s);

    {
        VERIOBJC_SCOPED_STACK(s);
        veriobjc_stack_alloc(s, 256);
        CHECK(veriobjc_stack_used(s) > level1_used);
        // Inner scope exits here
    }

    // Inner scope reset should have restored to level1_used
    LONGS_EQUAL(level1_used, veriobjc_stack_used(s));
    // Outer scope exits at end of function
}

TEST(VeriObjC_ScopeAndLayout, NestedScopedStackCleanup) {
    simulate_nested_scopes(&stack);
    LONGS_EQUAL(0, veriobjc_stack_used(&stack));
    POINTERS_EQUAL(buffer, stack.top);
}

// Contract 5: Defensive null guards for scope cleanup hook
TEST(VeriObjC_ScopeAndLayout, ScopeCleanupNullGuards) {
    // Calling cleanup with null or null stack must not crash
    veriobjc_scope_cleanup(nullptr);

    struct veriobjc_scope_guard empty_guard = { .stack = nullptr, .mark = nullptr };
    veriobjc_scope_cleanup(&empty_guard);
    VeriObjC_ScopeRelease(&empty_guard);
}
