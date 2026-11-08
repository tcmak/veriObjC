// Negative test: runtime method surgery (method_exchangeImplementations) is banned
typedef struct objc_method *Method;
extern void method_exchangeImplementations(Method m1, Method m2);

void swizzle_methods(Method a, Method b) {
    method_exchangeImplementations(a, b);
}
