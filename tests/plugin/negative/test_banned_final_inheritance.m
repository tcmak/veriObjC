// Negative test: inheriting from a class marked __attribute__((objc_final)) is banned
__attribute__((objc_root_class))
__attribute__((objc_sealed))
__attribute__((objc_final))
@interface FinalCodec
- (void)encode;
@end

__attribute__((objc_sealed))
@interface IllegalSubCodec : FinalCodec
- (void)encode;
@end
