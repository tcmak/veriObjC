// examples/vexecution_irq_pipeline.m
// Demonstrates Task 3.12 (<vstd/execution.h>):
//   - P2300-style immutable caller-stack sender pipeline (vsender_just_i32 / vsender_then_i32)
//   - Stack-allocated vasync future woken via hardware interrupt hook (vasync_irq_wake)
#include "veriobjc_runtime.h"
#include "vstd/execution.h"

vasync_task_def(NicRxIrqFuture, int32_t);

static int32_t decode_tag(int32_t x) {
    return x + 12;
}

static int32_t apply_fee(int32_t x) {
    return x * 2;
}

int32_t run_execution_irq_demo(void) {
    vtask_dag_edge("nic_irq", "decode_tag", 6);
    vtask_dag_edge("decode_tag", "apply_fee", 4);
    uint32_t wcet = vtask_dag_critical_path_wcet();

    struct vsender_i32 s0 = vsender_just_i32(9);
    struct vsender_i32 s1 = vsender_then_i32(s0, decode_tag);
    struct vsender_i32 s2 = vsender_then_i32(s1, apply_fee);
    int32_t computed = vsender_sync_wait_i32(s2);

    NicRxIrqFuture fut;
    vasync_task_init(&fut, 32u);
    vasync_irq_wake(&fut, computed + (int32_t)wcet);
    return vasync_task_is_ready(&fut) ? fut.result_val : -1;
}

#ifdef VERIOBJC_EXAMPLE_MAIN
int main(void) {
    return (run_execution_irq_demo() == 52) ? 0 : 1;
}
#endif
