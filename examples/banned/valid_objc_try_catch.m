// valid_objc_try_catch.m
// Valid stock Objective-C. Banned in VeriObjC: DWARF @try / @catch exception unwinding.
// Replacement: 'return err from f' zero-table multi-frame error returns.

int parse_with_objc_exceptions(int input) {
    @try {
        return input + 1;
    } @catch (...) {
        return -1;
    }
}
