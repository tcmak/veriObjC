// Negative test: sending a message to an untyped Class or dynamic receiver is banned
typedef struct veriobjc_class *Class;

void test_dynamic_send(Class dynamic_cls) {
    [dynamic_cls allocate];
}
