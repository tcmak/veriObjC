#include "vvector_header_no_objc_end.h"

const char *preserve_angle_bracket_string_literal(void) {
    return "vvector<int, 4>";
}

int32_t verify_header_vvector_capacities_by_value(void) {
    VVector(int32_t, 8) v8 = build_header_vec8(10);
    VVector(int32_t, 16) v16 = build_header_vec16(5);

    const char *hdr_lit = vvector_header_literal_probe();
    int32_t *a = vvector_at(&v8, 7u);
    int32_t *b = vvector_at(&v16, 15u);
    if (!hdr_lit || !a || !b) {
        return -1;
    }
    return (int32_t)vvector_cap(&v8) + (int32_t)vvector_cap(&v16) + *a + *b;
}
