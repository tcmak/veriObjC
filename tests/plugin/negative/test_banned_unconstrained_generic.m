// Negative test: unconstrained generic type parameter `<__covariant T>` or unsealed bound `<T : Unsealed *>` is banned at definition site
#include "veriobjc_runtime.h"

__attribute__((objc_root_class))
@interface UnsealedQuote {
@public
    int32_t price;
}
@end

__attribute__((objc_root_class))
__attribute__((objc_sealed))
__attribute__((objc_final))
@interface UnconstrainedVault<__covariant T : UnsealedQuote *> {
@public
    int32_t count;
}
@end

@implementation UnconstrainedVault
@end
