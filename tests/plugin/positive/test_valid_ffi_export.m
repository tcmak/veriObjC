// Positive test: @export FFI boundary checking (flat values + veriobjc_handle) and C header generation
#include "veriobjc_runtime.h"

typedef struct veriobjc_packed {
    uint16_t msg_len;
    uint16_t venue_id;
    uint32_t seq_num;
} WireOrderHeader;

vstatic_assert(sizeof(WireOrderHeader) == 8, "WireOrderHeader layout must be pinned to 8 bytes");

veriobjc_export
veriobjc_pre("hdr.msg_len <= 1400")
veriobjc_post("return.generation != 0")
struct veriobjc_handle export_ingest_wire_order(WireOrderHeader hdr, struct veriobjc_handle session_h) {
    if (hdr.msg_len == 0u) {
        return session_h;
    }
    return session_h;
}
