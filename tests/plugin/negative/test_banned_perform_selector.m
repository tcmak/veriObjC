// Negative test: performSelector: / performSelector:afterDelay: deferred dynamic dispatch is banned
__attribute__((objc_root_class))
__attribute__((objc_sealed))
__attribute__((objc_final))
@interface CommandActor
- (void)performSelector:(SEL)aSelector;
@end

void deferred_call(CommandActor * _Nonnull actor, SEL sel) {
    [actor performSelector:sel];
}
