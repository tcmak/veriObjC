// Negative test: zipping two veriStream wire streams of mismatched widths N is banned at compile time
#include "veriobjc_runtime.h"

vstream_def(Stream512, int8_t, 512);
vstream_def(Stream256, int8_t, 256);

void zip_mismatched_streams(Stream512 a, Stream256 b) {
    vstream_zip(a, b);
}
