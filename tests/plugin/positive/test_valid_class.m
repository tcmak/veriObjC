// Positive test: freestanding root class with nominal message dispatch compiles cleanly
@interface AudioFilter
- (void)process;
@end

@implementation AudioFilter
- (void)process {}
@end

void test_nominal_msg(AudioFilter *filter) {
    if (filter) {
        [filter process];
    }
}
