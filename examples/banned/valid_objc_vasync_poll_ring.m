// EXPECTED_DIAGNOSTIC: VeriObjC Violation: Banned polling ring in vasync pipeline
// Valid C/Objective-C loop, banned in VeriObjC because vasync wake-up must be interrupt-driven without busy-wait polling.
#include "veriobjc_runtime.h"
#include "vstd/execution.h"

vasync_task_def(SpinTask, int32_t);

void spin_wait_on_vasync(SpinTask *t) {
    while (!vasync_task_is_ready(t)) {
    }
}
