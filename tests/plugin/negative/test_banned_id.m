// Negative test: use of generic 'id' type must fail compilation under VeriObjC
void test_banned_id_variable(void) {
    id bad_var = (id)0;
    (void)bad_var;
}
