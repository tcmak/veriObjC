// valid_objc_nsobject_inheritance.m
// Valid stock Objective-C. Banned in VeriObjC: inheritance from NSObject.
// Replacement: @owned freestanding classes.

__attribute__((objc_root_class))
@interface NSObject
@end

@interface LegacyAudioController : NSObject
- (int)sampleRate;
@end
