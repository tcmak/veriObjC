// Negative test: inheritance hierarchy without __attribute__((objc_sealed)) is banned
__attribute__((objc_root_class))
@interface OpenAudioNode
- (void)process;
@end

@interface UnsealedOscillator : OpenAudioNode
- (void)process;
@end
