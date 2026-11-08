// valid_objc_dynamic_property.m
// Valid stock Objective-C. Banned in VeriObjC: @dynamic property synthesis.
// Replacement: Checked, packed @owned ivars.

__attribute__((objc_root_class))
@interface CoreDataRow
@property (nonatomic, assign) int rowVersion;
@end

@implementation CoreDataRow
@dynamic rowVersion;
@end
