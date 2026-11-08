// valid_objc_gcd_pipeline.m
// Valid stock Objective-C. Banned in VeriObjC: runtime thread / GCD scheduling on constrained paths.
// Replacement: Static topology and vasync fixed DAGs.

typedef void *dispatch_queue_t;
extern void dispatch_async(dispatch_queue_t queue, void (*work)(void));

void enqueue_gcd_stage(dispatch_queue_t q, void (*stage)(void)) {
    dispatch_async(q, stage);
}
