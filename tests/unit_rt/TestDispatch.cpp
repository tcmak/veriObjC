#include "CppUTest/TestHarness.h"
#include <stdint.h>
#include <stddef.h>

extern "C" {
#include "veriobjc_runtime.h"
#include "rt/dispatch.h"
}

TEST_GROUP(VeriObjC_Dispatch) {
    void setup() override {}
    void teardown() override {}
};

// Contract 1: Deterministic 32-bit FNV-1a selector hashing
TEST(VeriObjC_Dispatch, SelectorHashIsDeterministic) {
    // Null and empty string edge cases
    LONGS_EQUAL(0u, veriobjc_sel_hash(nullptr));
    LONGS_EQUAL(0x811c9dc5u, veriobjc_sel_hash(""));

    uint32_t h1 = veriobjc_sel_hash("processPacket:");
    uint32_t h2 = veriobjc_sel_hash("processPacket:");
    uint32_t h3 = veriobjc_sel_hash("calculatePeakGain:");

    LONGS_EQUAL(h1, h2);
    CHECK(h1 != 0u);
    CHECK(h3 != 0u);
    CHECK(h1 != h3);
}

static int dummy_method_a(void *self, int x) {
    (void)self;
    return x + 10;
}

static int dummy_method_b(void *self, int x) {
    (void)self;
    return x * 2;
}

// Contract 2: Link-time perfect-hashed jump table in .rodata resolves in O(1)
TEST(VeriObjC_Dispatch, StaticVTableResolvesMethodInConstantTime) {
    // Pre-computed or link-time selector IDs placed in conflict-free 4-slot table (mask = 3)
    constexpr uint32_t SEL_A = 0x10000001u; // (SEL_A >> 0) & 3 == 1
    constexpr uint32_t SEL_B = 0x20000002u; // (SEL_B >> 0) & 3 == 2

    static const struct veriobjc_vtable_entry rodata_entries[4] = {
        { 0u, nullptr },
        { SEL_A, (veriobjc_imp_t)dummy_method_a },
        { SEL_B, (veriobjc_imp_t)dummy_method_b },
        { 0u, nullptr }
    };

    static const struct veriobjc_vtable rodata_vtable = {
        .entries = rodata_entries,
        .mask = 3u,
        .shift = 0u
    };

    static const struct veriobjc_class synth_class = {
        .superclass = nullptr,
        .name = "AudioSynthesizer",
        .instance_size = sizeof(struct veriobjc_object),
        .vtable = &rodata_vtable
    };

    veriobjc_imp_t imp_a = veriobjc_dispatch_lookup(&synth_class, SEL_A);
    veriobjc_imp_t imp_b = veriobjc_dispatch_lookup(&synth_class, SEL_B);

    POINTERS_EQUAL((void *)dummy_method_a, (void *)imp_a);
    POINTERS_EQUAL((void *)dummy_method_b, (void *)imp_b);

    using fn_t = int (*)(void *, int);
    LONGS_EQUAL(15, ((fn_t)imp_a)(nullptr, 5));
    LONGS_EQUAL(10, ((fn_t)imp_b)(nullptr, 5));
}

// Contract 3: Selector ID tag verification, superclass fallback, and defensive null guards
TEST(VeriObjC_Dispatch, VTableSuperclassFallbackMissAndNullGuards) {
    constexpr uint32_t SEL_BASE    = 0x11110001u; // slot 1 (shift 0, mask 3)
    constexpr uint32_t SEL_DERIVED = 0x22220002u; // slot 2 (shift 0, mask 3)
    constexpr uint32_t SEL_COLLIDE = 0x99990002u; // also maps to slot 2, but wrong sel_id!

    static const struct veriobjc_vtable_entry base_entries[4] = {
        { 0u, nullptr },
        { SEL_BASE, (veriobjc_imp_t)dummy_method_a },
        { 0u, nullptr },
        { 0u, nullptr }
    };
    static const struct veriobjc_vtable base_vt = {
        .entries = base_entries,
        .mask = 3u,
        .shift = 0u
    };
    static const struct veriobjc_class base_class = {
        .superclass = nullptr,
        .name = "AudioNode",
        .instance_size = sizeof(struct veriobjc_object),
        .vtable = &base_vt
    };

    static const struct veriobjc_vtable_entry derived_entries[4] = {
        { 0u, nullptr },
        { 0u, nullptr },
        { SEL_DERIVED, (veriobjc_imp_t)dummy_method_b },
        { 0u, nullptr }
    };
    static const struct veriobjc_vtable derived_vt = {
        .entries = derived_entries,
        .mask = 3u,
        .shift = 0u
    };
    static const struct veriobjc_class derived_class = {
        .superclass = &base_class,
        .name = "SineOscillator",
        .instance_size = sizeof(struct veriobjc_object),
        .vtable = &derived_vt
    };

    // 1. Direct hit on derived class
    POINTERS_EQUAL((void *)dummy_method_b, (void *)veriobjc_dispatch_lookup(&derived_class, SEL_DERIVED));

    // 2. Fallback to superclass when not overridden in derived
    POINTERS_EQUAL((void *)dummy_method_a, (void *)veriobjc_dispatch_lookup(&derived_class, SEL_BASE));

    // 3. Bucket collision with mismatched sel_id must return nullptr (never invoke wrong method)
    POINTERS_EQUAL(nullptr, (void *)veriobjc_dispatch_lookup(&derived_class, SEL_COLLIDE));

    // 4. Null and invalid argument guards
    POINTERS_EQUAL(nullptr, (void *)veriobjc_dispatch_lookup(nullptr, SEL_BASE));
    POINTERS_EQUAL(nullptr, (void *)veriobjc_dispatch_lookup(&derived_class, 0u));

    static const struct veriobjc_vtable empty_vt = { .entries = nullptr, .mask = 0u, .shift = 0u };
    static const struct veriobjc_class class_no_entries = {
        .superclass = &base_class,
        .name = "NoEntriesNode",
        .instance_size = sizeof(struct veriobjc_object),
        .vtable = &empty_vt
    };
    static const struct veriobjc_class class_null_vt = {
        .superclass = &class_no_entries,
        .name = "NullVtNode",
        .instance_size = sizeof(struct veriobjc_object),
        .vtable = nullptr
    };
    POINTERS_EQUAL((void *)dummy_method_a, (void *)veriobjc_dispatch_lookup(&class_null_vt, SEL_BASE));
}

// Contract 4: Object-level dispatch via isa header on stack-allocated instances
TEST(VeriObjC_Dispatch, ObjectDispatchResolvesViaIsaAndGuardsNullObject) {
    constexpr uint32_t SEL_A = 0x10000001u;
    static const struct veriobjc_vtable_entry entries[2] = {
        { 0u, nullptr },
        { SEL_A, (veriobjc_imp_t)dummy_method_a }
    };
    static const struct veriobjc_vtable vt = {
        .entries = entries,
        .mask = 1u,
        .shift = 0u
    };
    static const struct veriobjc_class cls = {
        .superclass = nullptr,
        .name = "FilterNode",
        .instance_size = sizeof(struct veriobjc_object),
        .vtable = &vt
    };

    alignas(16) uint8_t buf[128];
    struct veriobjc_stack stack;
    veriobjc_stack_init(&stack, buf, sizeof(buf));

    struct veriobjc_object *obj = veriobjc_alloc_init(&cls, &stack);
    CHECK(obj != nullptr);

    POINTERS_EQUAL((void *)dummy_method_a, (void *)veriobjc_object_dispatch(obj, SEL_A));
    POINTERS_EQUAL(nullptr, (void *)veriobjc_object_dispatch(nullptr, SEL_A));
    POINTERS_EQUAL(nullptr, (void *)veriobjc_dispatch_by_sel_name(nullptr, "processPacket:"));
    POINTERS_EQUAL(nullptr, (void *)veriobjc_dispatch_by_sel_name(obj, nullptr));
    POINTERS_EQUAL(nullptr, (void *)veriobjc_dispatch_by_sel_name(obj, "unregistered:"));
}

// Contract 5: Closed-world 16-bit @sealed type tag & prefix-header arena allocation
TEST(VeriObjC_Dispatch, SealedTypeTagAndArenaPrefixAllocation) {
    // Null and empty class names return tag 0; valid class names fold FNV-1a-32 to non-zero 16-bit tags
    LONGS_EQUAL(0u, veriobjc_type_tag_from_name(nullptr));
    LONGS_EQUAL(0u, veriobjc_type_tag_from_name(""));

    veriobjc_type_tag_t tag_base = veriobjc_type_tag_from_name("SealedComputeNode");
    veriobjc_type_tag_t tag_sub1 = veriobjc_type_tag_from_name("LinearComputeNode");
    veriobjc_type_tag_t tag_sub2 = veriobjc_type_tag_from_name("AffineComputeNode");
    CHECK(tag_base != 0u);
    CHECK(tag_sub1 != 0u);
    CHECK(tag_sub2 != 0u);
    CHECK(tag_base != tag_sub1);
    CHECK(tag_sub1 != tag_sub2);
    LONGS_EQUAL(tag_sub1, veriobjc_type_tag_from_name("LinearComputeNode"));

    struct DummySubPayload {
        int32_t base_bias;
        int32_t slope;
        int32_t input_x;
    };

    alignas(16) uint8_t buf[64];
    for (size_t i = 0; i < sizeof(buf); ++i) {
        buf[i] = 0xAAu;
    }
    struct veriobjc_stack stack;
    veriobjc_stack_init(&stack, buf, sizeof(buf));

    // Null / invalid guards on veriobjc_sealed_alloc and veriobjc_sealed_tag
    POINTERS_EQUAL(nullptr, veriobjc_sealed_alloc(nullptr, sizeof(DummySubPayload), tag_sub1));
    POINTERS_EQUAL(nullptr, veriobjc_sealed_alloc(&stack, sizeof(DummySubPayload), 0u));
    POINTERS_EQUAL(nullptr, veriobjc_sealed_alloc(&stack, SIZE_MAX, tag_sub1));
    POINTERS_EQUAL(nullptr, veriobjc_sealed_alloc(&stack, sizeof(buf), tag_sub1));
    LONGS_EQUAL(0u, veriobjc_sealed_tag(nullptr));

    {
        VERIOBJC_SCOPED_STACK(&stack);
        DummySubPayload *sub = veriobjc_alloc_sealed(DummySubPayload, &stack);
        CHECK(sub != nullptr);
        // Zero-initialized payload at offset 0 and prefix tag at [-8]
        LONGS_EQUAL(0, sub->base_bias);
        LONGS_EQUAL(0, sub->slope);
        LONGS_EQUAL(0, sub->input_x);
        LONGS_EQUAL(veriobjc_type_tag_from_name("DummySubPayload"), veriobjc_sealed_tag(sub));

        // Zero-ivar / 0-byte instance allocation still places a valid 8-byte prefix header
        void *empty_inst = veriobjc_sealed_alloc(&stack, 0u, tag_base);
        CHECK(empty_inst != nullptr);
        LONGS_EQUAL(tag_base, veriobjc_sealed_tag(empty_inst));
    }

    LONGS_EQUAL(0u, veriobjc_stack_used(&stack));
}
