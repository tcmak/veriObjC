// Negative test: calling a function that executes `return err from parse_packet` outside an active `parse_packet` frame is banned
#include "veriobjc_runtime.h"

static int32_t parse_inner_tag(int32_t tag) {
    if (tag <= 0) {
        veriobjc_return_err_from(-10, parse_packet);
    }
    return tag;
}

int32_t unguarded_external_caller(int32_t raw_tag) {
    // Violation: unguarded_external_caller is not inside a `parse_packet` frame!
    return parse_inner_tag(raw_tag);
}
