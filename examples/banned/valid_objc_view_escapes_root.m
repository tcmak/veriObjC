// Valid stock Objective-C/C: assigning an inner-scope slice view to an outer variable.
// Rejected by VeriObjC (Task 3.1): view escapes its data-stack root scope.
#include <vstd/string.h>

void valid_objc_view_escape(struct veriobjc_stack *st) {
    struct vstring_view escaped = vstring_view_empty();
    {
        VERIOBJC_SCOPED_STACK(st);
        char *buf = (char *)veriobjc_stack_alloc(st, 16);
        struct vstring_view local_view = vstring_view_init(buf, 16);
        escaped = local_view;
    }
    (void)escaped;
}
