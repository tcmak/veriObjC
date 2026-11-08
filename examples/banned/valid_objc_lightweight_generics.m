// valid_objc_lightweight_generics.m
// Valid stock Objective-C. Banned in VeriObjC: type-erased lightweight generics.
// Replacement: Real constrained vgeneric with monomorphization.

__attribute__((objc_root_class))
@interface NSString
@end

__attribute__((objc_root_class))
@interface NSArray<__covariant ObjectType>
- (unsigned long)count;
@end

unsigned long measure_erased_array(NSArray<NSString *> * _Nonnull items) {
    return [items count];
}
