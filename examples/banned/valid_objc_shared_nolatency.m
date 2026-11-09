// Valid stock C/Objective-C: retaining a shared object on a fast path.
// Rejected by VeriObjC: @shared retain is permitted only in the @cold quarantine.

__attribute__((annotate("objc_effect_shared_retain")))
extern void veriobjc_shared_retain(void *obj);

__attribute__((annotate("objc_nolatency")))
void valid_objc_shared_in_hot_loop(void *obj) {
    veriobjc_shared_retain(obj);
}
