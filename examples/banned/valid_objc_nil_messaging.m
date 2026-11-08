// valid_objc_nil_messaging.m
// Valid stock Objective-C (returns 0/nil silently). Banned in VeriObjC: nil messaging on unproven pointer.
// Replacement: vvariant for optional absence; proven non-null pointers via contracts.

__attribute__((objc_root_class))
@interface TelemetryProbe
- (int)readCounter;
@end

int poll_optional_probe(TelemetryProbe * _Nullable probe) {
    return [probe readCounter];
}
