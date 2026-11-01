// Negative test: calling dynamic runtime reflection functions like NSClassFromString is banned
void *NSClassFromString(const char *name);

void test_dynamic_reflection(void) {
    void *cls = NSClassFromString("PacketHandler");
    (void)cls;
}
