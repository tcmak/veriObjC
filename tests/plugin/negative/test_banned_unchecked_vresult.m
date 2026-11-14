// Negative test: accessing vresult.value without proving res.is_ok in an enclosing branch
#include <vstd/result.h>

vresult_def(OrderResult, uint32_t, int32_t);

uint32_t unchecked_extract(OrderResult res) {
    return res.value; // Banned: .is_ok not proven!
}
