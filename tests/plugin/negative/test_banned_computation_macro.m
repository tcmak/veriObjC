// Negative test: preprocessor statement-expression computation macros are banned; use vconstexpr
#define BANNED_COMPUTE_SQUARE(x) ({ int _tmp = (x); _tmp * _tmp; })

int run_macro_codegen(int v) {
    return BANNED_COMPUTE_SQUARE(v);
}
