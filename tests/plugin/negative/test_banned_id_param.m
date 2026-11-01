// Negative test: function parameters declared as 'id' must fail compilation
void bad_func(id untyped_param) {
    (void)untyped_param;
}
