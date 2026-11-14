#include "veriobjc_runtime.h"
#include "vstd/contracts.h"

vcontract_pre("qty > 0 && qty <= 1000")
vcontract_post("result >= 0")
veriobjc_export veriobjc_pure int64_t validate_order_qty(int64_t qty) {
    if (!vcontract_in_range_i64(qty, 1, 1000)) {
        return 0;
    }
    return qty;
}
