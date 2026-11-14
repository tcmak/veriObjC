// ===========================================================================
// VeriObjC Phase 3 (Task 3.1): <vstd/string.h> & <vstd/span.h>
// Zero-copy text views, bounded fixed strings, and contiguous DMA slices
// ===========================================================================

#include <vstd/string.h>
#include <vstd/span.h>

vspan_def(WireWordSpan, uint32_t);
vfixed_string_def(VenueTag8, 8);

veriobjc_pure
uint32_t inspect_dma_packet(void) {
    uint32_t dma_words[4] = { 0x564F424Au, 100u, 200u, 300u };
    WireWordSpan window = vspan_from_array(WireWordSpan, dma_words);

    struct vstring_view raw_msg = vstring_view_FromLiteral("NASDAQ:AAPL");
    struct vstring_view venue = vstring_view_slice(raw_msg, 0u, 6u);

    VenueTag8 tag;
    vfixed_string_init(&tag);
    if (!vfixed_string_copy(&tag, venue)) {
        return 0u;
    }

    uint32_t *second_word = vspan_at(&window, 1u);
    if (second_word && vstring_view_eq(vfixed_string_view(&tag), vstring_view_FromLiteral("NASDAQ"))) {
        return *second_word + (uint32_t)tag.len;
    }
    return 0u;
}
