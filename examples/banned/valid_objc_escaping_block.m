// valid_objc_escaping_block.m
// Valid stock Objective-C. Banned in VeriObjC: escaping function block.
// Replacement: Non-escaping blocks monomorphized at call site.

typedef int (^SampleTransformBlock)(int);

SampleTransformBlock make_escaping_adder(void) {
    return ^int(int x) {
        return x + 1;
    };
}
