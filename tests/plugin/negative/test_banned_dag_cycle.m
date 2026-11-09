// Negative test: cycle in static task DAG must fail compile-time deadlock-freedom proof
#include "veriobjc_runtime.h"

void configure_cyclic_pipeline(void) {
    veriobjc_dag_edge("rx_decode", "risk_check", 12);
    veriobjc_dag_edge("risk_check", "tx_encode", 8);
    veriobjc_dag_edge("tx_encode", "rx_decode", 5);
}
