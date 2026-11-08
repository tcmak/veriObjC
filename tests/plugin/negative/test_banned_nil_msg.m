// Negative test: messaging nil or an unproven __nullable pointer is banned
__attribute__((objc_root_class))
__attribute__((objc_sealed))
__attribute__((objc_final))
@interface SensorNode
- (int)readSample;
@end

int dispatch_on_nullable(SensorNode * _Nullable maybe_node) {
    return [maybe_node readSample];
}
