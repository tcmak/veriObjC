// valid_objc_kvc_roundtrip.m
// Valid stock Objective-C. Banned in VeriObjC: key-value coding/observation.
// Replacement: @reflect compile-time field offsets and vweak observation.

__attribute__((objc_root_class))
@interface SynthPatch
- (void)setValue:(const char *)value forKey:(const char *)key;
@end

void apply_kvc_parameter(SynthPatch * _Nonnull patch) {
    [patch setValue:"440" forKey:"cutoffHz"];
}
