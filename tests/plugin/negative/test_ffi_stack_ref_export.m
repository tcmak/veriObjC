// Negative test: exporting a data-stack reference across an FFI boundary is banned
#include "veriobjc_runtime.h"

extern void foreign_c_sink(int *ptr);

void export_stack_pointer_to_ffi(struct veriobjc_stack *s) {
    int *local_ref = (int *)veriobjc_stack_alloc(s, sizeof(int));
    foreign_c_sink(local_ref);
}
