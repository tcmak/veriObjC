// Negative test: vstring_view rooted at an inner scope cannot escape to an outer variable
#include <vstd/string.h>

void escape_view_from_inner_scope(struct veriobjc_stack *st) {
    struct vstring_view outer_view = vstring_view_empty();
    {
        VERIOBJC_SCOPED_STACK(st);
        char *local_buf = (char *)veriobjc_stack_alloc(st, 32);
        struct vstring_view inner_view = vstring_view_init(local_buf, 32);
        outer_view = inner_view;
    }
    (void)outer_view;
}
