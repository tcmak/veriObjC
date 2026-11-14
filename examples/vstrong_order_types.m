// ===========================================================================
// VeriObjC Phase 3 (Task 3.6): <vstd/strong.h>
// Nominal strong newtypes with zero implicit conversions
// ===========================================================================

#include <vstd/strong.h>

vstrong_def(TickPrice, int64_t);
vstrong_def(LotSize, int32_t);
vstrong_def(SequenceId, uint64_t);

veriobjc_pure
int64_t evaluate_limit_order(int64_t raw_px, int32_t raw_lots, uint64_t raw_seq) {
    auto limit_px = vstrong_make(TickPrice, raw_px);
    auto ask_px   = vstrong_make(TickPrice, 10050);
    auto lots     = vstrong_make(LotSize, raw_lots);
    auto seq      = vstrong_make(SequenceId, raw_seq);

    if (vstrong_lt(limit_px, ask_px) && vstrong_unwrap(seq) > 0u) {
        return vstrong_unwrap(limit_px) * (int64_t)vstrong_unwrap(lots);
    }
    return 0;
}
