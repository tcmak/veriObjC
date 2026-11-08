// Negative test: backreferences or lookaround assertions in vregex patterns are banned at compile time
#include "veriobjc_runtime.h"

bool match_invalid_backref(const char *buf, size_t len) {
    return vregex_match("([A-Z]+)\\1", buf, len);
}
