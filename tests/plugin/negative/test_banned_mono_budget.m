// Negative test: exceeding per-package monomorphization instantiation budget fails the build
#include "veriobjc_runtime.h"

void exceed_mono_budget(void) {
    vmono_instantiate("order_book_pkg", "FrameStore", 512, 2);
    vmono_instantiate("order_book_pkg", "FrameStore", 512, 2);
    vmono_instantiate("order_book_pkg", "FrameStore", 512, 2);
}
