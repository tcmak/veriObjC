// Positive test: <vstd/string.h> and <vstd/span.h> zero-copy views
#include <vstd/string.h>
#include <vstd/span.h>

vspan_def(ByteSpan, uint8_t);

veriobjc_pure
uint32_t parse_wire_slice(void) {
    uint8_t dma_frame[8] = { 'V', 'O', 'B', 'J', 1, 2, 3, 4 };
    ByteSpan sp = vspan_from_array(ByteSpan, dma_frame);
    struct vstring_view hdr = vstring_view_FromLiteral("VOBJ");

    uint8_t *first = vspan_at(&sp, 0u);
    if (first && vstring_view_at(hdr, 0u) == (char)(*first)) {
        return (uint32_t)sp.len + (uint32_t)hdr.len;
    }
    return 0u;
}
