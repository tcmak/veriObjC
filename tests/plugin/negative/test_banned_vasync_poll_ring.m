// EXPECTED_DIAGNOSTIC: VeriObjC Violation: Banned polling ring in vasync pipeline
#include "veriobjc_runtime.h"
#include "vstd/execution.h"

vasync_task_def(SpinTask, int32_t);

void spin_wait_on_vasync(SpinTask *t) {
    // Busy-wait polling loop on vasync_task_is_ready is banned; wake-up must be interrupt-driven
    while (!vasync_task_is_ready(t)) {
    }
}
