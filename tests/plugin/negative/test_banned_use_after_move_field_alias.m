// Negative plugin test (Task 6.0.8b):
// Struct/field alias use-after-move:
//   1. Taking &s.field (or &s), moving s via veriobjc_move(s), then reading via the alias.
//   2. Moving s.ptr_field via veriobjc_move(s.ptr_field), then reading s.ptr_field or its alias.
#include "veriobjc_runtime.h"

typedef struct {
    uint32_t seq;
    int32_t  qty;
} MoveableEnvelope;

typedef struct {
    int32_t *buf;
    uint32_t tag;
} BufferHolder;

static void consume_envelope(MoveableEnvelope env) {
    (void)env;
}

static void consume_int_ptr(int32_t *p) {
    (void)p;
}

int32_t illegal_read_field_alias_after_struct_move(void) {
    MoveableEnvelope env = { .seq = 1u, .qty = 42 };
    int32_t *qty_alias = &env.qty;
    consume_envelope(veriobjc_move(env));
    return *qty_alias;
}

int32_t illegal_read_after_member_field_move(int32_t *incoming) {
    if (!incoming) return 0;
    BufferHolder holder = { .buf = incoming, .tag = 9u };
    int32_t *buf_alias = holder.buf;
    consume_int_ptr(veriobjc_move(holder.buf));
    int32_t a = *buf_alias;
    int32_t b = *holder.buf;
    return a + b;
}
