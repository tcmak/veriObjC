// Negative test: @export functions may only pass flat values or opaque veriobjc_handle across FFI
#include "veriobjc_runtime.h"

struct NonFlatExportRecord {
    uint32_t id;
    void *raw_internal_ptr;
};

veriobjc_export
uint32_t export_bad_boundary(struct NonFlatExportRecord rec) {
    return rec.id;
}
