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

// Contract 6: Move semantics intrinsic and flat-value SPSC channel operations & edge cases
TEST(VeriObjC_ScopeAndLayout, MoveIntrinsicAndFlatChannelOperations) {
    HeaderlessParticle p1 = { 1.0f, 2.0f, 42u };
    HeaderlessParticle p2 = veriobjc_move(p1);
    LONGS_EQUAL(42u, p2.id);
    LONGS_EQUAL(-7, __veriobjc_return_err_from(-7, "test_frame"));
    CHECK_FALSE(__veriobjc_regex_match("A+", "A", 1));

    // DFA table walker & null/edge guards
    uint8_t trans[2 * 256];
    memset(trans, 0, sizeof(trans));
    trans[0 * 256 + (uint8_t)'A'] = 1u;
    trans[1 * 256 + (uint8_t)'A'] = 1u;
    uint8_t accept[2] = { 0u, 1u };
    struct veriobjc_dfa dfa = { trans, accept, 2u, 0u };

    CHECK_FALSE(veriobjc_dfa_match(nullptr, "A", 1));
    struct veriobjc_dfa bad_dfa1 = { nullptr, accept, 2u, 0u };
    CHECK_FALSE(veriobjc_dfa_match(&bad_dfa1, "A", 1));
    struct veriobjc_dfa bad_dfa2 = { trans, nullptr, 2u, 0u };
    CHECK_FALSE(veriobjc_dfa_match(&bad_dfa2, "A", 1));
    struct veriobjc_dfa bad_dfa3 = { trans, accept, 0u, 0u };
    CHECK_FALSE(veriobjc_dfa_match(&bad_dfa3, "A", 1));
    CHECK_FALSE(veriobjc_dfa_match(&dfa, nullptr, 1));
    CHECK_FALSE(veriobjc_dfa_match(&dfa, nullptr, 0));
    CHECK_TRUE(veriobjc_dfa_match(&dfa, "AA", 2));
    CHECK_FALSE(veriobjc_dfa_match(&dfa, "AB", 2));

    // Null guard on channel init
    veriobjc_channel_init(nullptr, nullptr, sizeof(HeaderlessParticle), 2);

    HeaderlessParticle ring[2];
    struct veriobjc_channel ch;
    veriobjc_channel_init(&ch, ring, sizeof(HeaderlessParticle), 2);

    // Defensive null / invalid size / empty capacity guards on send & recv
    CHECK_FALSE(veriobjc_channel_send(nullptr, &p2, sizeof(HeaderlessParticle)));
    CHECK_FALSE(veriobjc_channel_send(&ch, nullptr, sizeof(HeaderlessParticle)));
    CHECK_FALSE(veriobjc_channel_send(&ch, &p2, sizeof(HeaderlessParticle) - 1));

    struct veriobjc_channel no_ring_ch;
    veriobjc_channel_init(&no_ring_ch, nullptr, sizeof(HeaderlessParticle), 2);
    CHECK_FALSE(veriobjc_channel_send(&no_ring_ch, &p2, sizeof(HeaderlessParticle)));
    CHECK_FALSE(veriobjc_channel_recv(&no_ring_ch, &p1, sizeof(HeaderlessParticle)));

    struct veriobjc_channel zero_cap_ch;
    veriobjc_channel_init(&zero_cap_ch, ring, sizeof(HeaderlessParticle), 0);
    CHECK_FALSE(veriobjc_channel_send(&zero_cap_ch, &p2, sizeof(HeaderlessParticle)));

    HeaderlessParticle out = { 0.0f, 0.0f, 0u };
    CHECK_FALSE(veriobjc_channel_recv(nullptr, &out, sizeof(HeaderlessParticle)));
    CHECK_FALSE(veriobjc_channel_recv(&ch, nullptr, sizeof(HeaderlessParticle)));
    CHECK_FALSE(veriobjc_channel_recv(&ch, &out, sizeof(HeaderlessParticle) - 1));
    // Empty channel recv fails
    CHECK_FALSE(veriobjc_channel_recv(&ch, &out, sizeof(HeaderlessParticle)));

    // Send 2 items (fills capacity 2)
    HeaderlessParticle m1 = { 3.0f, 4.0f, 100u };
    HeaderlessParticle m2 = { 5.0f, 6.0f, 200u };
    CHECK_TRUE(veriobjc_channel_send(&ch, &m1, sizeof(HeaderlessParticle)));
    CHECK_TRUE(veriobjc_channel_send(&ch, &m2, sizeof(HeaderlessParticle)));
    // 3rd send fails when full
    CHECK_FALSE(veriobjc_channel_send(&ch, &m1, sizeof(HeaderlessParticle)));

    // Recv 2 items in FIFO order
    CHECK_TRUE(veriobjc_channel_recv(&ch, &out, sizeof(HeaderlessParticle)));
    LONGS_EQUAL(100u, out.id);
    CHECK_TRUE(veriobjc_channel_recv(&ch, &out, sizeof(HeaderlessParticle)));
    LONGS_EQUAL(200u, out.id);
    CHECK_FALSE(veriobjc_channel_recv(&ch, &out, sizeof(HeaderlessParticle)));
}
