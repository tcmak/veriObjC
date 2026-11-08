// valid_objc_category.m
// Valid stock Objective-C. Banned in VeriObjC: open-world categories.
// Replacement: Method overloads and sealed protocols.

__attribute__((objc_root_class))
@interface CodecPacket
- (int)payloadBytes;
@end

@interface CodecPacket (DiagnosticsAdditions)
- (int)checksumWord;
@end
