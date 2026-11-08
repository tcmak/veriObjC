// valid_objc_category_swizzle.m
// Valid stock Objective-C. Banned in VeriObjC: runtime method surgery (swizzling).
// Replacement: Immutable link-time hashed method tables.

typedef struct objc_method *Method;
extern void method_exchangeImplementations(Method m1, Method m2);

void install_method_swizzle(Method original, Method replacement) {
    method_exchangeImplementations(original, replacement);
}
