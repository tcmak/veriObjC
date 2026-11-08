// Negative test: runtime thread / GCD queue spawning on constrained paths is banned
typedef void *dispatch_queue_t;
extern void dispatch_async(dispatch_queue_t queue, void (*work)(void));

void spawn_runtime_work(dispatch_queue_t q, void (*fn)(void)) {
    dispatch_async(q, fn);
}
