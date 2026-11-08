// Negative test: Key-Value Coding / Observation (setValue:forKey:, valueForKey:, addObserver:) is banned
__attribute__((objc_root_class))
__attribute__((objc_sealed))
__attribute__((objc_final))
@interface ConfigRecord
- (void)setValue:(const char *)val forKey:(const char *)key;
@end

void mutate_via_kvc(ConfigRecord * _Nonnull rec) {
    [rec setValue:"100" forKey:"gain"];
}
