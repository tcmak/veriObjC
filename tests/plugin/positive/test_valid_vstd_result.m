// Positive test: <vstd/result.h> branch-proven vresult<T, E> access
#include <vstd/result.h>

vresult_def(FrameDecodeResult, uint32_t, int32_t);

veriobjc_pure
uint32_t decode_checked(uint32_t raw_len) {
    FrameDecodeResult res = (raw_len > 0u)
        ? vresult_ok(FrameDecodeResult, raw_len * 2u)
        : vresult_err(FrameDecodeResult, -1);

    if (res.is_ok) {
        return res.value;
    }
    return (uint32_t)res.err;
}
