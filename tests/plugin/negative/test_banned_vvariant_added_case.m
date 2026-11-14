// EXPECTED_DIAGNOSTIC: VeriObjC Violation: Non-exhaustive vmatch on vvariant
#include "veriobjc_runtime.h"
#include "vstd/variant.h"

typedef enum : uint32_t {
    EXEC_NEW = 0,
    EXEC_PARTIAL = 1,
    EXEC_EXPIRED = 2
} ExecStateTag;

vvariant_def(ExecStateVar, ExecStateTag, {
    int32_t new_id;
    int32_t partial_qty;
    int32_t expired_ts;
});

int32_t match_missing_added_case(ExecStateVar st) {
    // Missing EXEC_EXPIRED case after variant enum expansion
    vmatch(st) {
        case EXEC_NEW:
            return st.payload.new_id;
        case EXEC_PARTIAL:
            return st.payload.partial_qty;
        default:
            return 0;
    }
    return 0;
}
