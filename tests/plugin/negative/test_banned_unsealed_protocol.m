// Negative test: conforming to an unsealed @protocol is banned in closed-world dispatch
@protocol UnsealedStreamProtocol
- (void)flushStream;
@end

__attribute__((objc_root_class))
__attribute__((objc_sealed))
@interface StreamNode <UnsealedStreamProtocol>
- (void)flushStream;
@end
