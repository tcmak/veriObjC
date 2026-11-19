#include "CppUTest/TestHarness.h"
#include "veriobjc_runtime.h"
#include "vstd/vector.h"
#include "vstd/map.h"
#include "vstd/hash.h"
#include "vstd/variant.h"
#include "vstd/channel.h"
#include "vstd/atomic.h"
#include "vstd/stream.h"
#include "vstd/ternary.h"

using IntVec4 = vvector<int32_t, 4>;
vflat_map_def(OrderFlatMap4, uint64_t, int32_t, 4);
vhash_flat_def(OrderHashMap8, uint64_t, int32_t, 8);

typedef enum : uint32_t {
    ORDER_EVT_NEW = 0,
    ORDER_EVT_FILL = 1,
    ORDER_EVT_CANCEL = 2
} OrderEvtTag;

vvariant_def(OrderEvtVariant, OrderEvtTag, {
    int32_t new_qty;
    int32_t fill_qty;
    uint32_t cancel_code;
});

vspsc_channel_def(TelemetryChan4, int32_t, 4);
vatomic_def(AtomicCounter, int32_t);
vbits_def(Bits2Wire, 2);
vstream_def(ActStream4, int32_t, 4);
vstream_token_shift_def(TokenShift4, int32_t, 4);
vstream_kv_arena_def(KVArena4, int32_t, 2, 4);

static void build_vec_slot_inplace(int32_t *out_slot, int32_t seed) {
    if (out_slot) {
        *out_slot = seed * 10;
    }
}

TEST_GROUP(VstdWave2And3Group) {
};

TEST(VstdWave2And3Group, InplaceVectorOperationsAndBounds) {
    IntVec4 vec;
    vvector_init(&vec);
    vvector_init((IntVec4 *)nullptr);

    LONGS_EQUAL(0u, vvector_len(&vec));
    LONGS_EQUAL(4u, vvector_cap(&vec));
    LONGS_EQUAL(0u, vvector_len((const IntVec4 *)nullptr));
    LONGS_EQUAL(0u, vvector_cap((const IntVec4 *)nullptr));

    CHECK_FALSE(vvector_push((IntVec4 *)nullptr, 1));
    CHECK_TRUE(vvector_push(&vec, 11));
    CHECK_TRUE(vvector_push(&vec, 22));
    CHECK_TRUE(vvector_push_inplace(&vec, build_vec_slot_inplace, 3));
    CHECK_TRUE(vvector_push(&vec, 44));
    CHECK_FALSE(vvector_push(&vec, 55));
    CHECK_FALSE(vvector_push_inplace(&vec, build_vec_slot_inplace, 6));
    CHECK_FALSE(vvector_push_inplace((IntVec4 *)nullptr, build_vec_slot_inplace, 6));

    LONGS_EQUAL(4u, vvector_len(&vec));
    LONGS_EQUAL(11, *vvector_at(&vec, 0u));
    LONGS_EQUAL(22, *vvector_at(&vec, 1u));
    LONGS_EQUAL(30, *vvector_at(&vec, 2u));
    LONGS_EQUAL(44, *vvector_at(&vec, 3u));
    POINTERS_EQUAL(nullptr, vvector_at(&vec, 4u));
    POINTERS_EQUAL(nullptr, vvector_at((const IntVec4 *)nullptr, 0u));
}

#ifdef __OBJC__
TEST(VstdWave2And3Group, VVectorGenericObjectiveCDirectMethods) {
    struct {
        uint32_t count;
        uint32_t cap;
        VVectorItem *buf[VVECTOR_GENERIC_CAPACITY];
    } raw_vec;
    struct {
        uint32_t item_tag;
    } it0 = {101u}, it1 = {202u}, it2 = {303u}, it3 = {404u}, it4 = {505u};

    VVector *vec = (VVector *)&raw_vec;
    __veriobjc_direct_VVector__initVec((VVector *)nullptr);
    __veriobjc_direct_VVector__initVec(vec);

    LONGS_EQUAL(0u, __veriobjc_direct_VVector__len(vec));
    LONGS_EQUAL(VVECTOR_GENERIC_CAPACITY, __veriobjc_direct_VVector__cap(vec));
    LONGS_EQUAL(0u, __veriobjc_direct_VVector__len((VVector *)nullptr));
    LONGS_EQUAL(0u, __veriobjc_direct_VVector__cap((VVector *)nullptr));

    CHECK_FALSE(__veriobjc_direct_VVector__push_((VVector *)nullptr, (VVectorItem *)&it0));
    CHECK_TRUE(__veriobjc_direct_VVector__push_(vec, (VVectorItem *)&it0));
    CHECK_TRUE(__veriobjc_direct_VVector__push_(vec, (VVectorItem *)&it1));
    CHECK_TRUE(__veriobjc_direct_VVector__push_(vec, (VVectorItem *)&it2));
    CHECK_TRUE(__veriobjc_direct_VVector__push_(vec, (VVectorItem *)&it3));
    CHECK_FALSE(__veriobjc_direct_VVector__push_(vec, (VVectorItem *)&it4));

    LONGS_EQUAL(4u, __veriobjc_direct_VVector__len(vec));
    POINTERS_EQUAL((VVectorItem *)&it0, __veriobjc_direct_VVector__at_(vec, 0u));
    POINTERS_EQUAL((VVectorItem *)&it3, __veriobjc_direct_VVector__at_(vec, 3u));
    POINTERS_EQUAL(nullptr, __veriobjc_direct_VVector__at_(vec, 4u));
    POINTERS_EQUAL(nullptr, __veriobjc_direct_VVector__at_((VVector *)nullptr, 0u));
}
#endif

TEST(VstdWave2And3Group, FlatMapSortedBinarySearchAndDeterministicOrder) {
    OrderFlatMap4 fmap;
    vflat_map_init(&fmap);
    vflat_map_init((OrderFlatMap4 *)nullptr);

    CHECK_FALSE(vflat_map_insert_u64((OrderFlatMap4 *)nullptr, 10u, 100));
    CHECK_TRUE(vflat_map_insert_u64(&fmap, 30u, 300));
    CHECK_TRUE(vflat_map_insert_u64(&fmap, 10u, 100));
    CHECK_TRUE(vflat_map_insert_u64(&fmap, 20u, 200));
    // Update existing key in-place
    CHECK_TRUE(vflat_map_insert_u64(&fmap, 20u, 250));
    CHECK_TRUE(vflat_map_insert_u64(&fmap, 40u, 400));
    // Full capacity rejection (no reallocation)
    CHECK_FALSE(vflat_map_insert_u64(&fmap, 50u, 500));

    // Deterministic sorted key order: 10, 20, 30, 40
    LONGS_EQUAL(10u, fmap.keys[0]);
    LONGS_EQUAL(20u, fmap.keys[1]);
    LONGS_EQUAL(30u, fmap.keys[2]);
    LONGS_EQUAL(40u, fmap.keys[3]);

    LONGS_EQUAL(100, *vflat_map_find_u64(&fmap, 10u));
    LONGS_EQUAL(250, *vflat_map_find_u64(&fmap, 20u));
    LONGS_EQUAL(300, *vflat_map_find_u64(&fmap, 30u));
    LONGS_EQUAL(400, *vflat_map_find_u64(&fmap, 40u));
    POINTERS_EQUAL(nullptr, vflat_map_find_u64(&fmap, 5u));
    POINTERS_EQUAL(nullptr, vflat_map_find_u64(&fmap, 25u));
    POINTERS_EQUAL(nullptr, vflat_map_find_u64(&fmap, 99u));
    POINTERS_EQUAL(nullptr, vflat_map_find_u64((OrderFlatMap4 *)nullptr, 10u));
}

TEST(VstdWave2And3Group, FlatHashMapOpenAddressingCollisionsAndLoadCeiling) {
    OrderHashMap8 hmap;
    vhash_flat_init(&hmap);
    vhash_flat_init((OrderHashMap8 *)nullptr);

    CHECK_FALSE(vhash_flat_insert_u64((OrderHashMap8 *)nullptr, 1u, 10));
    // Insert keys that collide under mask (capacity 8) and test linear probing
    CHECK_TRUE(vhash_flat_insert_u64(&hmap, 1u, 10));
    CHECK_TRUE(vhash_flat_insert_u64(&hmap, 9u, 90));
    // Update existing key in-place (both initial slot and probed slot)
    CHECK_TRUE(vhash_flat_insert_u64(&hmap, 1u, 11));
    CHECK_TRUE(vhash_flat_insert_u64(&hmap, 9u, 99));

    CHECK_TRUE(vhash_flat_insert_u64(&hmap, 2u, 20));
    CHECK_TRUE(vhash_flat_insert_u64(&hmap, 3u, 30));
    CHECK_TRUE(vhash_flat_insert_u64(&hmap, 4u, 40));
    CHECK_TRUE(vhash_flat_insert_u64(&hmap, 5u, 50));
    // Load factor ceiling for cap=8 is 6 elements (75% = 6/8); 7th new key is rejected without rehash
    CHECK_FALSE(vhash_flat_insert_u64(&hmap, 6u, 60));

    LONGS_EQUAL(11, *vhash_flat_find_u64(&hmap, 1u));
    LONGS_EQUAL(99, *vhash_flat_find_u64(&hmap, 9u));
    LONGS_EQUAL(50, *vhash_flat_find_u64(&hmap, 5u));
    POINTERS_EQUAL(nullptr, vhash_flat_find_u64(&hmap, 17u));
    POINTERS_EQUAL(nullptr, vhash_flat_find_u64(&hmap, 999u));
    POINTERS_EQUAL(nullptr, vhash_flat_find_u64((OrderHashMap8 *)nullptr, 1u));
}

TEST(VstdWave2And3Group, VariantSizeAuditAndJumpTableMatch) {
    // Exact size = tag (4 bytes) + max union member (4 bytes) = 8 bytes
    LONGS_EQUAL(8u, sizeof(OrderEvtVariant));

    OrderEvtVariant ev_new = vvariant_make(OrderEvtVariant, ORDER_EVT_NEW, new_qty, 50);
    LONGS_EQUAL(ORDER_EVT_NEW, vvariant_tag(ev_new));
    OrderEvtVariant ev_fill = vvariant_make(OrderEvtVariant, ORDER_EVT_FILL, fill_qty, 25);
    OrderEvtVariant ev_cancel = vvariant_make(OrderEvtVariant, ORDER_EVT_CANCEL, cancel_code, 7u);

    OrderEvtVariant events[3] = {ev_new, ev_fill, ev_cancel};
    int32_t acc = 0;
    for (int i = 0; i < 3; ++i) {
        vmatch(events[i]) {
            case ORDER_EVT_NEW:
                acc += events[i].payload.new_qty;
                break;
            case ORDER_EVT_FILL:
                acc -= events[i].payload.fill_qty;
                break;
            case ORDER_EVT_CANCEL:
                acc += (int32_t)events[i].payload.cancel_code;
                break;
        }
    }
    LONGS_EQUAL(32, acc);
}

TEST(VstdWave2And3Group, SPSCChannelAndScopedLinearAtomic) {
    TelemetryChan4 chan;
    vspsc_channel_init(&chan);
    vspsc_channel_init((TelemetryChan4 *)nullptr);

    int32_t out_val = 0;
    CHECK_FALSE(vspsc_channel_recv(&chan, &out_val));
    CHECK_FALSE(vspsc_channel_recv((TelemetryChan4 *)nullptr, &out_val));
    CHECK_FALSE(vspsc_channel_recv(&chan, (int32_t *)nullptr));
    CHECK_FALSE(vspsc_channel_send((TelemetryChan4 *)nullptr, 1));

    CHECK_TRUE(vspsc_channel_send(&chan, 101));
    CHECK_TRUE(vspsc_channel_send(&chan, 202));
    CHECK_TRUE(vspsc_channel_send(&chan, 303));
    CHECK_TRUE(vspsc_channel_send(&chan, 404));
    CHECK_FALSE(vspsc_channel_send(&chan, 505));

    CHECK_TRUE(vspsc_channel_recv(&chan, &out_val));
    LONGS_EQUAL(101, out_val);
    CHECK_TRUE(vspsc_channel_recv(&chan, &out_val));
    LONGS_EQUAL(202, out_val);

    AtomicCounter counter;
    vatomic_init(&counter, 40);
    vatomic_init((AtomicCounter *)nullptr, 0);
    {
        vatomic_acquire(guard, &counter);
        int32_t *val_ptr = vatomic_get(&guard);
        CHECK_TRUE(val_ptr != nullptr);
        *val_ptr += 2;
        LONGS_EQUAL(1u, counter.lock_state);
    }
    LONGS_EQUAL(0u, counter.lock_state);

    {
        vatomic_acquire(null_guard, (AtomicCounter *)nullptr);
        POINTERS_EQUAL(nullptr, vatomic_get(&null_guard));
    }
}

TEST(VstdWave2And3Group, StreamingAndTernarySubstrateVerification) {
    Bits2Wire w = vbits_make(Bits2Wire, 2, 0b10u);
    LONGS_EQUAL(0b10u, w.bits);

    uint8_t packed = vternary_pack4(
        VTERNARY_POS_ONE, VTERNARY_NEG_ONE, VTERNARY_ZERO, VTERNARY_POS_ONE);
    LONGS_EQUAL(VTERNARY_POS_ONE, vternary_unpack_at(packed, 0u));
    LONGS_EQUAL(VTERNARY_NEG_ONE, vternary_unpack_at(packed, 1u));
    LONGS_EQUAL(VTERNARY_ZERO, vternary_unpack_at(packed, 2u));
    LONGS_EQUAL(VTERNARY_POS_ONE, vternary_unpack_at(packed, 3u));

    int32_t acts[4] = {10, 20, 30, 40};
    int32_t dot = vternary_dot_packed(acts, &packed, 1u);
    // +10 - 20 + 0 + 40 = 30
    LONGS_EQUAL(30, dot);
    LONGS_EQUAL(0, vternary_dot_packed(nullptr, &packed, 1u));
    LONGS_EQUAL(0, vternary_dot_packed(acts, nullptr, 1u));

    // Verify weight tape via veriobjc_to_bytes / vternary_verify_weights_from_bytes
    alignas(8) uint8_t tape_buf[64];
    uint8_t valid_weights[4] = {packed, packed, packed, packed};
    size_t written = veriobjc_to_bytes(valid_weights, 1u, (uint32_t)sizeof(valid_weights), 0x5452u, tape_buf, sizeof(tape_buf));
    CHECK_TRUE(written > 0u);

    const uint8_t *verified_ptr = nullptr;
    CHECK_TRUE(vternary_verify_weights_from_bytes(tape_buf, written, 0x5452u, (uint32_t)sizeof(valid_weights), &verified_ptr));
    CHECK_TRUE(verified_ptr != nullptr);
    CHECK_FALSE(vternary_verify_weights_from_bytes(nullptr, written, 0x5452u, (uint32_t)sizeof(valid_weights), &verified_ptr));
    CHECK_FALSE(vternary_verify_weights_from_bytes(tape_buf, written, 0x5452u, 2u, &verified_ptr));
    CHECK_FALSE(vternary_verify_weights_from_bytes(tape_buf, written, 0x5452u, (uint32_t)sizeof(valid_weights), nullptr));

    // Corrupt a weight byte with reserved 0b11 code -> must reject before any kernel runs
    uint8_t bad_weights[4] = {vternary_pack4(VTERNARY_INVALID, 0, 0, 0), 0, 0, 0};
    size_t bad_written = veriobjc_to_bytes(bad_weights, 1u, (uint32_t)sizeof(bad_weights), 0x5452u, tape_buf, sizeof(tape_buf));
    CHECK_FALSE(vternary_verify_weights_from_bytes(tape_buf, bad_written, 0x5452u, (uint32_t)sizeof(bad_weights), &verified_ptr));

    // Token shift (1-cycle register delay), GLU mux gate (0-cycle), shift-only RMSNorm
    TokenShift4 shift_reg;
    vstream_token_shift_init(&shift_reg);
    vstream_token_shift_init((TokenShift4 *)nullptr);

    int32_t tok0[4] = {1, 2, 3, 4};
    int32_t tok1[4] = {5, 6, 7, 8};
    int32_t mixed[4] = {0, 0, 0, 0};
    vstream_token_shift_step(&shift_reg, tok0, mixed);
    // First token: lower half from zero-initialized prev_token (0, 0), upper half from tok0 (3, 4)
    LONGS_EQUAL(0, mixed[0]);
    LONGS_EQUAL(0, mixed[1]);
    LONGS_EQUAL(3, mixed[2]);
    LONGS_EQUAL(4, mixed[3]);

    vstream_token_shift_step(&shift_reg, tok1, mixed);
    // Second token: lower half from tok0 (1, 2), upper half from tok1 (7, 8)
    LONGS_EQUAL(1, mixed[0]);
    LONGS_EQUAL(2, mixed[1]);
    LONGS_EQUAL(7, mixed[2]);
    LONGS_EQUAL(8, mixed[3]);
    vstream_token_shift_step((TokenShift4 *)nullptr, tok1, mixed);

    LONGS_EQUAL(42, vternary_glu_mux_gate(42, 1));
    LONGS_EQUAL(0, vternary_glu_mux_gate(42, 0));
    LONGS_EQUAL(0, vternary_glu_mux_gate(42, -1));
    LONGS_EQUAL(16, vternary_rmsnorm_shift(64, 2u));

    // Pre-banked KV cache arena with ContextCap = 4
    KVArena4 kv_arena;
    vstream_kv_arena_init(&kv_arena);
    vstream_kv_arena_init((KVArena4 *)nullptr);
    int32_t kv_pair[2] = {111, 222};
    CHECK_FALSE(vstream_kv_append((KVArena4 *)nullptr, 0u, kv_pair));
    CHECK_FALSE(vstream_kv_append(&kv_arena, 0u, nullptr));
    CHECK_TRUE(vstream_kv_append(&kv_arena, 0u, kv_pair));
    CHECK_FALSE(vstream_kv_append(&kv_arena, 4u, kv_pair));
    const int32_t *slot0 = vstream_kv_at(&kv_arena, 0u);
    CHECK_TRUE(slot0 != nullptr);
    LONGS_EQUAL(111, slot0[0]);
    LONGS_EQUAL(222, slot0[1]);
    POINTERS_EQUAL(nullptr, vstream_kv_at(&kv_arena, 4u));
    POINTERS_EQUAL(nullptr, vstream_kv_at((const KVArena4 *)nullptr, 0u));
}
