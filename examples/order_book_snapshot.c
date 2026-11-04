#include "veriobjc_runtime.h"

#define TAG_ORDER_BOOK_LEVEL 0x4F42u // "OB"

// Position-independent Order Book Level using 32-bit self-relative links
typedef struct {
    uint32_t price_ticks;
    uint32_t total_qty;
    __relative(32) next_better;
    __relative(32) next_worse;
} OrderBookLevel;

// Freestanding round-trip persistence and verification of an order-book checkpoint
int checkpoint_and_verify_order_book(void *scratch_arena, size_t arena_size) {
    if (!scratch_arena || arena_size < 1024) {
        return -1;
    }

    struct veriobjc_stack stack;
    veriobjc_stack_init(&stack, scratch_arena, arena_size);

    VERIOBJC_SCOPED_STACK(&stack);

    OrderBookLevel *levels = (OrderBookLevel *)veriobjc_stack_alloc(&stack, 3 * sizeof(OrderBookLevel));
    uint8_t *wire_buf = (uint8_t *)veriobjc_stack_alloc(&stack, 256);
    if (!levels || !wire_buf) {
        return -2;
    }

    // Initialize 3 contiguous price levels with self-relative links
    levels[0].price_ticks = 10050;
    levels[0].total_qty   = 500;
    levels[1].price_ticks = 10045;
    levels[1].total_qty   = 1200;
    levels[2].price_ticks = 10040;
    levels[2].total_qty   = 800;

    veriobjc_rel32_set(&levels[0].next_better, NULL);
    veriobjc_rel32_set(&levels[0].next_worse, &levels[1]);
    veriobjc_rel32_set(&levels[1].next_better, &levels[0]);
    veriobjc_rel32_set(&levels[1].next_worse, &levels[2]);
    veriobjc_rel32_set(&levels[2].next_better, &levels[1]);
    veriobjc_rel32_set(&levels[2].next_worse, NULL);

    // 1. Serialize framed snapshot (to_bytes)
    size_t frame_len = veriobjc_to_bytes(
        levels, sizeof(OrderBookLevel), 3, TAG_ORDER_BOOK_LEVEL, wire_buf, 256
    );
    if (frame_len == 0) {
        return -3;
    }

    // 2. Verify and load zero-copy snapshot (from_bytes)
    uint32_t loaded_count = 0;
    const OrderBookLevel *loaded = (const OrderBookLevel *)veriobjc_from_bytes(
        wire_buf, frame_len, TAG_ORDER_BOOK_LEVEL, sizeof(OrderBookLevel), &loaded_count
    );
    if (!loaded || loaded_count != 3) {
        return -4;
    }

    // 3. Verify all self-relative links lie strictly on element boundaries within payload
    size_t payload_bytes = loaded_count * sizeof(OrderBookLevel);
    for (uint32_t i = 0; i < loaded_count; ++i) {
        if (!veriobjc_verify_rel32_link(&loaded[i].next_better, loaded, payload_bytes, sizeof(OrderBookLevel)) ||
            !veriobjc_verify_rel32_link(&loaded[i].next_worse, loaded, payload_bytes, sizeof(OrderBookLevel))) {
            return -5;
        }
    }

    // 4. Traverse self-relative links inside the relocated wire buffer
    const OrderBookLevel *second = (const OrderBookLevel *)veriobjc_rel32_get(&loaded[0].next_worse);
    if (!second || second->price_ticks != 10045) {
        return -6;
    }

    // 5. Tamper with a byte and confirm from_bytes rejects corrupted snapshot
    wire_buf[frame_len - 1] ^= 0x01u;
    if (veriobjc_from_bytes(wire_buf, frame_len, TAG_ORDER_BOOK_LEVEL, sizeof(OrderBookLevel), NULL) != NULL) {
        return -7;
    }

    return 0;
}
