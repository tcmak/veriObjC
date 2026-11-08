#include "veriobjc_runtime.h"

__attribute__((objc_root_class))
__attribute__((objc_sealed))
__attribute__((objc_final))
@interface FixTagValidator {
@public
    uint32_t session_id;
}
- (bool)isValidInstrumentSymbol:(const char *)buf length:(size_t)len;
- (bool)isValidFixHeader:(const char *)buf length:(size_t)len;
@end

@implementation FixTagValidator
- (bool)isValidInstrumentSymbol:(const char *)buf length:(size_t)len {
    return vregex_match("[A-Z]+[0-9]*", buf, len);
}

- (bool)isValidFixHeader:(const char *)buf length:(size_t)len {
    return vregex_match("8=FIX(42|44|50)", buf, len);
}
@end

bool validate_incoming_fix_frame(
    FixTagValidator *validator,
    const char *hdr_buf,
    size_t hdr_len,
    const char *sym_buf,
    size_t sym_len
) {
    if (!validator) {
        return false;
    }
    if (![validator isValidFixHeader:hdr_buf length:hdr_len]) {
        return false;
    }
    return [validator isValidInstrumentSymbol:sym_buf length:sym_len];
}
