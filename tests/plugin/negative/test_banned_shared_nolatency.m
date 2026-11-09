// Negative test: @shared reference counting is permitted only inside @cold quarantine
#include "veriobjc_runtime.h"

veriobjc_effect_shared_retain
extern void veriobjc_shared_retain(void *obj);

veriobjc_nolatency
void retain_in_hot_path(void *shared_obj) {
    veriobjc_shared_retain(shared_obj);
}
