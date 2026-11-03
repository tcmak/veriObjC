#include "CppUTest/TestHarness.h"
#include <stdint.h>
#include <stddef.h>
#include <string.h>

extern "C" {
#include "veriobjc_runtime.h"
}

typedef struct {
    uint32_t key;
    __relative(8)  next8;
    __relative(16) next16;
    __relative(32) left32;
    __relative(32) right32;
} RelNode;

TEST_GROUP(VeriObjC_TapeAndRelPtr) {
    void setup() override {}
    void teardown() override {}
};

// Contract 1: Self-relative links (8/16/32-bit) resolve position-independently across raw memcpy
TEST(VeriObjC_TapeAndRelPtr, SelfRelativeLinksSurviveBufferRelocationWithoutFixups) {
    RelNode arena_a[3];
    memset(arena_a, 0, sizeof(arena_a));

    arena_a[0].key = 100;
    arena_a[1].key = 50;
    arena_a[2].key = 150;

    // Link node 0 -> node 1 (forward) and node 2 -> node 0 (backward)
    CHECK_TRUE(veriobjc_rel8_set(&arena_a[0].next8, &arena_a[1]));
    CHECK_TRUE(veriobjc_rel16_set(&arena_a[2].next16, &arena_a[0]));
    CHECK_TRUE(veriobjc_rel32_set(&arena_a[0].left32, &arena_a[1]));
    CHECK_TRUE(veriobjc_rel32_set(&arena_a[0].right32, &arena_a[2]));
    CHECK_TRUE(veriobjc_rel8_set(&arena_a[1].next8, nullptr));
    CHECK_TRUE(veriobjc_rel16_set(&arena_a[1].next16, nullptr));
    CHECK_TRUE(veriobjc_rel32_set(&arena_a[1].left32, nullptr));

    // Relocate entire array to a completely separate memory buffer via raw memcpy
    RelNode arena_b[3];
    memcpy(arena_b, arena_a, sizeof(arena_a));

    // Resolved pointers in arena_b must point into arena_b, NOT arena_a!
    POINTERS_EQUAL(&arena_b[1], veriobjc_rel8_get(&arena_b[0].next8));
    POINTERS_EQUAL(&arena_b[0], veriobjc_rel16_get(&arena_b[2].next16));
    POINTERS_EQUAL(&arena_b[1], veriobjc_rel32_get(&arena_b[0].left32));
    POINTERS_EQUAL(&arena_b[2], veriobjc_rel32_get(&arena_b[0].right32));
    POINTERS_EQUAL(nullptr, veriobjc_rel8_get(&arena_b[1].next8));
    POINTERS_EQUAL(nullptr, veriobjc_rel16_get(&arena_b[1].next16));
    POINTERS_EQUAL(nullptr, veriobjc_rel32_get(&arena_b[1].left32));
}

// Contract 2: Narrow offset range bounds, self-reference sentinel guard, and null safety
TEST(VeriObjC_TapeAndRelPtr, SelfRelativeLinksRejectRangeOverflowSelfLinkAndNulls) {
    uint8_t large_buf[70000];
    struct veriobjc_rel8  *r8  = (struct veriobjc_rel8 *)&large_buf[35000];
    struct veriobjc_rel16 *r16 = (struct veriobjc_rel16 *)&large_buf[35000];
    struct veriobjc_rel32 *r32 = (struct veriobjc_rel32 *)&large_buf[35000];

    // 1. Null link pointer guards
    CHECK_FALSE(veriobjc_rel8_set(nullptr, large_buf));
    CHECK_FALSE(veriobjc_rel16_set(nullptr, large_buf));
    CHECK_FALSE(veriobjc_rel32_set(nullptr, large_buf));
    POINTERS_EQUAL(nullptr, veriobjc_rel8_get(nullptr));
    POINTERS_EQUAL(nullptr, veriobjc_rel16_get(nullptr));
    POINTERS_EQUAL(nullptr, veriobjc_rel32_get(nullptr));

    // 2. Self-pointing (delta == 0) must be rejected so it never aliases NULL sentinel
    CHECK_FALSE(veriobjc_rel8_set(r8, r8));
    CHECK_FALSE(veriobjc_rel16_set(r16, r16));
    CHECK_FALSE(veriobjc_rel32_set(r32, r32));

    // 3. Out-of-range forward and backward offsets for 8-bit [-128, 127] and 16-bit [-32768, 32767]
    CHECK_FALSE(veriobjc_rel8_set(r8, (const uint8_t *)r8 + 128));
    CHECK_FALSE(veriobjc_rel8_set(r8, (const uint8_t *)r8 - 129));
    CHECK_TRUE(veriobjc_rel8_set(r8, (const uint8_t *)r8 + 127));
    CHECK_TRUE(veriobjc_rel8_set(r8, (const uint8_t *)r8 - 128));

    CHECK_FALSE(veriobjc_rel16_set(r16, (const uint8_t *)r16 + 32768));
    CHECK_FALSE(veriobjc_rel16_set(r16, (const uint8_t *)r16 - 32769));
    CHECK_TRUE(veriobjc_rel16_set(r16, (const uint8_t *)r16 + 32767));
    CHECK_TRUE(veriobjc_rel16_set(r16, (const uint8_t *)r16 - 32768));

    // 4. Out-of-range 32-bit offsets on 64-bit address space
    const uint8_t *far_fwd = (const uint8_t *)r32 + (ptrdiff_t)INT32_MAX + 1;
    const uint8_t *far_bwd = (const uint8_t *)r32 + (ptrdiff_t)INT32_MIN - 1;
    CHECK_FALSE(veriobjc_rel32_set(r32, far_fwd));
    CHECK_FALSE(veriobjc_rel32_set(r32, far_bwd));
}

// Contract 3: Framed snapshot serialization (to_bytes) and zero-copy verified loading (from_bytes)
TEST(VeriObjC_TapeAndRelPtr, TapeToBytesAndFromBytesRoundTripsWithVerifiedLinks) {
    constexpr uint16_t TAG_RELNODE = 0x1042u;
    RelNode nodes[3];
    memset(nodes, 0, sizeof(nodes));

    nodes[0].key = 10;
    nodes[1].key = 20;
    nodes[2].key = 30;
    CHECK_TRUE(veriobjc_rel32_set(&nodes[0].left32, &nodes[1]));
    CHECK_TRUE(veriobjc_rel32_set(&nodes[0].right32, &nodes[2]));

    alignas(8) uint8_t wire_buf[256];
    size_t written = veriobjc_to_bytes(nodes, sizeof(RelNode), 3, TAG_RELNODE, wire_buf, sizeof(wire_buf));
    LONGS_EQUAL(sizeof(struct veriobjc_tape_header) + sizeof(nodes), written);

    uint32_t loaded_count = 0;
    const RelNode *loaded = (const RelNode *)veriobjc_from_bytes(
        wire_buf, written, TAG_RELNODE, sizeof(RelNode), &loaded_count
    );
    CHECK(loaded != nullptr);
    LONGS_EQUAL(3u, loaded_count);

    size_t payload_bytes = loaded_count * sizeof(RelNode);
    for (uint32_t i = 0; i < loaded_count; ++i) {
        CHECK_TRUE(veriobjc_verify_rel32_link(&loaded[i].left32, loaded, payload_bytes, sizeof(RelNode)));
        CHECK_TRUE(veriobjc_verify_rel32_link(&loaded[i].right32, loaded, payload_bytes, sizeof(RelNode)));
    }

    const RelNode *left  = (const RelNode *)veriobjc_rel32_get(&loaded[0].left32);
    const RelNode *right = (const RelNode *)veriobjc_rel32_get(&loaded[0].right32);
    CHECK(left != nullptr);
    CHECK(right != nullptr);
    LONGS_EQUAL(20u, left->key);
    LONGS_EQUAL(30u, right->key);
}

// Contract 4: Fuzz mutation sweep & hostile framing/link rejection
TEST(VeriObjC_TapeAndRelPtr, TapeFromBytesAndVerifyLinkRejectAllMutatedAndHostileInputs) {
    constexpr uint16_t TAG_RELNODE = 0x1042u;
    RelNode nodes[3];
    memset(nodes, 0, sizeof(nodes));
    nodes[0].key = 1;
    nodes[1].key = 2;
    nodes[2].key = 3;
    CHECK_TRUE(veriobjc_rel32_set(&nodes[0].left32, &nodes[1]));
    CHECK_TRUE(veriobjc_rel32_set(&nodes[1].right32, &nodes[2]));

    alignas(8) uint8_t wire_buf[256];
    size_t written = veriobjc_to_bytes(nodes, sizeof(RelNode), 3, TAG_RELNODE, wire_buf, sizeof(wire_buf));
    CHECK(written > sizeof(struct veriobjc_tape_header));

    // 1. Exhaustive single-byte mutation fuzz across every header & payload byte
    uint8_t mutated[256];
    const uint8_t masks[3] = { 0x01u, 0x80u, 0xFFu };
    for (size_t i = 0; i < written; ++i) {
        for (size_t m = 0; m < 3; ++m) {
            memcpy(mutated, wire_buf, written);
            mutated[i] ^= masks[m];
            uint32_t cnt = 999;
            POINTERS_EQUAL(
                nullptr,
                veriobjc_from_bytes(mutated, written, TAG_RELNODE, sizeof(RelNode), &cnt)
            );
        }
    }

    // 2. Hostile frames with recomputed checksums but invalid structural invariants
    // 2a. Mismatched payload_bytes vs elem_size * elem_count
    {
        memcpy(mutated, wire_buf, written);
        struct veriobjc_tape_header *hdr = (struct veriobjc_tape_header *)mutated;
        hdr->elem_count = 2; // mismatch with payload_bytes (3 * sizeof(RelNode))
        hdr->checksum = veriobjc_tape_checksum(hdr, mutated + sizeof(*hdr), hdr->payload_bytes);
        POINTERS_EQUAL(nullptr, veriobjc_from_bytes(mutated, written, TAG_RELNODE, sizeof(RelNode), nullptr));
    }
    // 2b. Wrong version with valid checksum
    {
        memcpy(mutated, wire_buf, written);
        struct veriobjc_tape_header *hdr = (struct veriobjc_tape_header *)mutated;
        hdr->version = 99u;
        hdr->checksum = veriobjc_tape_checksum(hdr, mutated + sizeof(*hdr), hdr->payload_bytes);
        POINTERS_EQUAL(nullptr, veriobjc_from_bytes(mutated, written, TAG_RELNODE, sizeof(RelNode), nullptr));
    }
    // 2c. Wrong magic with valid checksum
    {
        memcpy(mutated, wire_buf, written);
        struct veriobjc_tape_header *hdr = (struct veriobjc_tape_header *)mutated;
        hdr->magic = 0xDEADBEEFu;
        hdr->checksum = veriobjc_tape_checksum(hdr, mutated + sizeof(*hdr), hdr->payload_bytes);
        POINTERS_EQUAL(nullptr, veriobjc_from_bytes(mutated, written, TAG_RELNODE, sizeof(RelNode), nullptr));
    }
    // 2d. Wrong tag or elem_size or truncated/extra length
    POINTERS_EQUAL(nullptr, veriobjc_from_bytes(wire_buf, written, 0x9999u, sizeof(RelNode), nullptr));
    POINTERS_EQUAL(nullptr, veriobjc_from_bytes(wire_buf, written, TAG_RELNODE, sizeof(RelNode) + 4, nullptr));
    POINTERS_EQUAL(nullptr, veriobjc_from_bytes(wire_buf, written - 1, TAG_RELNODE, sizeof(RelNode), nullptr));
    POINTERS_EQUAL(nullptr, veriobjc_from_bytes(wire_buf, written + 1, TAG_RELNODE, sizeof(RelNode), nullptr));
    POINTERS_EQUAL(nullptr, veriobjc_from_bytes(wire_buf, sizeof(struct veriobjc_tape_header) - 1, TAG_RELNODE, sizeof(RelNode), nullptr));
    POINTERS_EQUAL(nullptr, veriobjc_from_bytes(nullptr, written, TAG_RELNODE, sizeof(RelNode), nullptr));
    POINTERS_EQUAL(nullptr, veriobjc_from_bytes(wire_buf, written, TAG_RELNODE, 0u, nullptr));

    // 2e. Valid load with nullptr out_elem_count
    CHECK(veriobjc_from_bytes(wire_buf, written, TAG_RELNODE, sizeof(RelNode), nullptr) != nullptr);

    // 3. veriobjc_to_bytes defensive boundary checks & empty payload round-trip
    LONGS_EQUAL(0u, veriobjc_to_bytes(nullptr, sizeof(RelNode), 3, TAG_RELNODE, wire_buf, sizeof(wire_buf)));
    LONGS_EQUAL(0u, veriobjc_to_bytes(nodes, sizeof(RelNode), 3, TAG_RELNODE, nullptr, sizeof(wire_buf)));
    LONGS_EQUAL(0u, veriobjc_to_bytes(nodes, 0u, 3, TAG_RELNODE, wire_buf, sizeof(wire_buf)));
    LONGS_EQUAL(0u, veriobjc_to_bytes(nodes, 0x100000u, 0x10000u, TAG_RELNODE, wire_buf, sizeof(wire_buf)));
    LONGS_EQUAL(0u, veriobjc_to_bytes(nodes, sizeof(RelNode), 3, TAG_RELNODE, wire_buf, written - 1));

    size_t empty_written = veriobjc_to_bytes(nullptr, sizeof(RelNode), 0, TAG_RELNODE, wire_buf, sizeof(wire_buf));
    LONGS_EQUAL(sizeof(struct veriobjc_tape_header), empty_written);
    uint32_t empty_cnt = 42;
    CHECK(veriobjc_from_bytes(wire_buf, empty_written, TAG_RELNODE, sizeof(RelNode), &empty_cnt) != nullptr);
    LONGS_EQUAL(0u, empty_cnt);

    // 4. Self-relative link verification against wild/unaligned/out-of-bounds offsets
    size_t payload_bytes = sizeof(nodes);
    CHECK_FALSE(veriobjc_verify_rel32_link(nullptr, nodes, payload_bytes, sizeof(RelNode)));
    CHECK_FALSE(veriobjc_verify_rel32_link(&nodes[0].left32, nullptr, payload_bytes, sizeof(RelNode)));
    CHECK_FALSE(veriobjc_verify_rel32_link(&nodes[0].left32, nodes, payload_bytes, 0u));
    CHECK_FALSE(veriobjc_verify_rel32_link(&nodes[0].left32, nodes, sizeof(RelNode) - 1, sizeof(RelNode)));

    // Link pointer itself outside the region
    struct veriobjc_rel32 outside_link = { .offset = 0 };
    CHECK_FALSE(veriobjc_verify_rel32_link(&outside_link, nodes, payload_bytes, sizeof(RelNode)));
    CHECK_FALSE(veriobjc_verify_rel32_link(&nodes[2].right32, nodes, sizeof(RelNode), sizeof(RelNode)));

    // Corrupt offset: points before payload_base
    nodes[0].left32.offset = -64;
    CHECK_FALSE(veriobjc_verify_rel32_link(&nodes[0].left32, nodes, payload_bytes, sizeof(RelNode)));

    // Corrupt offset: points past payload end
    nodes[0].left32.offset = (int32_t)payload_bytes + 16;
    CHECK_FALSE(veriobjc_verify_rel32_link(&nodes[0].left32, nodes, payload_bytes, sizeof(RelNode)));

    // Corrupt offset: points inside payload, but unaligned to element boundary
    int32_t valid_delta = (int32_t)((const uint8_t *)&nodes[1] - (const uint8_t *)&nodes[0].left32);
    nodes[0].left32.offset = valid_delta + 1; // +1 byte off element boundary!
    CHECK_FALSE(veriobjc_verify_rel32_link(&nodes[0].left32, nodes, payload_bytes, sizeof(RelNode)));
}
