// Positive test: Static core/NUMA topology, fixed task DAG WCET verification, and stack vasync
#include "veriobjc_runtime.h"

veriobjc_core_topology_def(kRxCore, 2, 0);
veriobjc_core_topology_def(kRiskCore, 3, 0);

uint32_t execute_telemetry_dag_tick(void) {
    veriobjc_dag_edge("dma_rx", "parse_frame", 15);
    veriobjc_dag_edge("parse_frame", "risk_gate", 25);
    veriobjc_dag_edge("risk_gate", "tx_ring", 10);

    uint32_t total_wcet = veriobjc_dag_critical_path_wcet();

    struct veriobjc_vasync_frame rx_future;
    veriobjc_vasync_init(&rx_future, kRxCore.core_id);
    veriobjc_vasync_wake_irq(&rx_future, 42u);

    if (veriobjc_vasync_is_ready(&rx_future)) {
        return total_wcet + rx_future.result_code + kRiskCore.core_id;
    }
    return total_wcet;
}
