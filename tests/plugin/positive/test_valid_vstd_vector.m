#include "veriobjc_runtime.h"
#include "vstd/vector.h"

vvector_def(BatchVec4, int32_t, 4);

static void parse_into_slot(int32_t *out_slot, int32_t raw) {
    if (out_slot) {
        *out_slot = raw + 100;
    }
}

int32_t run_valid_vvector(void) {
    BatchVec4 batch;
    vvector_init(&batch);
    vvector_push(&batch, 10);
    vvector_push_inplace(&batch, parse_into_slot, 20);
    int32_t *p = vvector_at(&batch, 1u);
    return p ? *p : 0;
}
