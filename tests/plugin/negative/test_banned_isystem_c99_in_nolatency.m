// Negative plugin test (Task 6.0.6):
//   1. Calling an unannotated extern C99 function from an -isystem header inside
//      a veriobjc_nolatency function still fails closed (defaults to @cold).
//   2. Defining a statement-expression computation macro directly in user .m code
//      (outside -isystem) remains banned.
#include "veriobjc_runtime.h"
#include <synth_c99_frame.h>

#define USER_LOCAL_READ_U16_LE(ptr) \
    ({ \
        const uint8_t *_u_p = (const uint8_t *)(ptr); \
        (uint16_t)(((uint16_t)_u_p[0]) | (uint16_t)(((uint16_t)_u_p[1]) << 8u)); \
    })

veriobjc_nolatency
uint32_t illegal_c99_extern_call_in_nolatency(const uint8_t *buf, size_t len) {
    return synth_c99_external_audit_crc(buf, len);
}

uint16_t illegal_user_defined_stmt_expr_macro(const uint8_t *buf) {
    return USER_LOCAL_READ_U16_LE(buf);
}
