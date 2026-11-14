#include "veriobjc_runtime.h"
#include "vstd/execution.h"

vasync_task_def(RxIrqTask, int32_t);

static int32_t scale_payload(int32_t v) {
    return v * 3;
}

int32_t run_valid_vstd_execution(void) {
    vtask_dag_edge("rx_dma", "decode_fix", 12);
    vtask_dag_edge("decode_fix", "risk_check", 8);
    uint32_t wcet = vtask_dag_critical_path_wcet();

    struct vsender_i32 pipe = vsender_then_i32(vsender_just_i32(10), scale_payload);
    int32_t val = vsender_sync_wait_i32(pipe);

    RxIrqTask irq_task;
    vasync_task_init(&irq_task, 1u);
    vasync_irq_wake(&irq_task, val + (int32_t)wcet);
    return vasync_task_is_ready(&irq_task) ? irq_task.result_val : 0;
}
