// Negative test: `auto` / `vauto` return type on a protocol method or `@export` function is banned
#include "veriobjc_runtime.h"

__attribute__((objc_sealed))
@protocol ExportedQuoteProtocol
- (vauto)computeExportedQuote:(int64_t)raw_ticks;
@end

veriobjc_export vauto exported_boundary_fn(int32_t x) {
    vauto res = { .__opaque = x };
    return res;
}
