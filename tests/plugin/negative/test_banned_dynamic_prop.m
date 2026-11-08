// Negative test: @dynamic properties are banned in closed-world VeriObjC
__attribute__((objc_root_class))
__attribute__((objc_sealed))
__attribute__((objc_final))
@interface DynamicTelemetry
@property (nonatomic, assign) int sampleRate;
@end

@implementation DynamicTelemetry
@dynamic sampleRate;
@end
