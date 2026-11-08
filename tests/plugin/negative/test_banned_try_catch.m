// Negative test: Objective-C @try / @catch / @finally exception unwinding is banned
void unwind_attempt(int x) {
    @try {
        (void)x;
    } @catch (...) {
    }
}
