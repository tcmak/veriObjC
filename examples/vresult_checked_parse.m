// ===========================================================================
// VeriObjC Phase 3 (Task 3.7): <vstd/result.h>
// Branch-proven vresult<T, E> error handling without stack unwinding
// ===========================================================================

#include <vstd/result.h>

vresult_def(HeaderParseResult, uint32_t, int32_t);

veriobjc_pure
HeaderParseResult parse_payload_length(uint32_t magic, uint32_t raw_len) {
    if (magic != 0x564F424Au) {
        return vresult_err(HeaderParseResult, -10);
    }
    if (raw_len > 1400u) {
        return vresult_err(HeaderParseResult, -20);
    }
    return vresult_ok(HeaderParseResult, raw_len);
}

veriobjc_pure
uint32_t process_validated_frame(uint32_t magic, uint32_t raw_len) {
    HeaderParseResult res = parse_payload_length(magic, raw_len);
    if (res.is_ok) {
        return res.value + 24u;
    }
    return 0u;
}
