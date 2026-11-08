// Negative test: lightweight erased generic hints (NSArray<T>) are banned
__attribute__((objc_root_class))
@interface NSString
@end

__attribute__((objc_root_class))
@interface NSArray<__covariant ObjectType>
- (unsigned long)count;
@end

unsigned long count_strings(NSArray<NSString *> * _Nonnull arr) {
    return [arr count];
}
