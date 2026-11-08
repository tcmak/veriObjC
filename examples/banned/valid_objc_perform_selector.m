// valid_objc_perform_selector.m
// Valid stock Objective-C. Banned in VeriObjC: deferred dynamic dispatch (performSelector:).
// Replacement: Direct calls or static task DAG.

__attribute__((objc_root_class))
@interface TimerTarget
- (void)performSelector:(SEL)aSelector;
@end

void trigger_deferred_selector(TimerTarget * _Nonnull target, SEL action) {
    [target performSelector:action];
}
