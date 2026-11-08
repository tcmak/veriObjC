// Negative test: Objective-C categories violate closed-world dispatch and are banned
__attribute__((objc_root_class))
__attribute__((objc_sealed))
__attribute__((objc_final))
@interface PacketFrame
- (int)length;
@end

@interface PacketFrame (ExtraMutations)
- (int)mutatedLength;
@end
