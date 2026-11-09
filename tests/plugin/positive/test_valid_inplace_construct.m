// Positive test: Built-in-place construction threading destination slot without intermediate copy
#include "veriobjc_runtime.h"

struct veriobjc_inplace_only OrderBookSnapshotSlot {
    uint64_t seq;
    uint32_t bid_px;
    uint32_t ask_px;
};

static inline void init_snapshot_slot(struct OrderBookSnapshotSlot *dst, uint64_t seq, uint32_t bid, uint32_t ask) {
    if (!dst) return;
    dst->seq = seq;
    dst->bid_px = bid;
    dst->ask_px = ask;
}

uint32_t record_snapshot_inplace(struct OrderBookSnapshotSlot *ring, uint32_t *count, uint32_t cap) {
    veriobjc_push_inplace(ring, *count, cap, init_snapshot_slot, 1001ull, 500u, 502u);
    return *count;
}
