// Negative test: inheriting from NSObject is banned in VeriObjC
@interface NSObject
@end

@interface PacketHandler : NSObject
@end
