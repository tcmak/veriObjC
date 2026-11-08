// valid_objc_untyped_messaging.m
// Valid stock Objective-C. Banned in VeriObjC: untyped / dynamic receiver messaging.
// Replacement: Nominal closed-world devirtualization.

__attribute__((objc_root_class))
@interface UntypedDispatcher
- (int)ping;
@end

int send_untyped_ping(Class cls) {
    return [cls ping];
}
