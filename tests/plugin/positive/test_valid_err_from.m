// Positive test: multi-frame error return (`return err from parse_packet`) statically verified within active frame and lowered
#include "veriobjc_runtime.h"

static int32_t verify_payload_length(int32_t len) {
    if (len <= 0 || len > 1024) {
        veriobjc_return_err_from(-1, parse_packet);
    }
    return len;
}

static int32_t decode_envelope(int32_t magic, int32_t len) {
    if (magic != 0x564F) {
        veriobjc_return_err_from(-2, parse_packet);
    }
    int32_t checked_len = verify_payload_length(len);
    if (checked_len < 0) {
        return checked_len;
    }
    return checked_len + 8;
}

int32_t parse_packet(int32_t magic, int32_t len) {
    return decode_envelope(magic, len);
}
