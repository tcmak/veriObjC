// valid_objc_runtime_reflection.m
// Valid stock Objective-C. Banned in VeriObjC: Apple runtime reflection.
// Replacement: @reflect() at compile time.

extern Class objc_getClass(const char *name);

Class lookup_driver_by_name(const char *driver_name) {
    return objc_getClass(driver_name);
}
