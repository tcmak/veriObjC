// Positive test: local nominal `auto` deduction preserves exact types (including vstrong) and root-checked borrows
#include "veriobjc_runtime.h"

vstrong_def(PriceTicks, int64_t);

static inline PriceTicks make_price(int64_t ticks) {
    PriceTicks p = { .raw = ticks };
    return p;
}

int64_t evaluate_local_auto(struct veriobjc_stack *stack) {
    if (!stack) {
        return 0;
    }
    VERIOBJC_SCOPED_STACK(stack);
    auto *scratch = (PriceTicks *)veriobjc_stack_alloc(stack, sizeof(PriceTicks));
    if (!scratch) {
        return 0;
    }
    scratch->raw = 12500;

    // vstrong never unwraps under `auto` inference: `inferred_price` has nominal type `PriceTicks`
    auto inferred_price = make_price(scratch->raw + 50);
    vauto_borrow(borrowed_price, inferred_price);
    return borrowed_price->raw;
}
