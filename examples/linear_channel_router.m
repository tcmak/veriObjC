#include "veriobjc_runtime.h"

__attribute__((objc_flat))
typedef struct {
    uint64_t sequence_num;
    int64_t  limit_price;
    uint32_t shares;
    uint16_t symbol_id;
    uint16_t side;
} ExecutionReport;

__attribute__((objc_root_class))
__attribute__((objc_sealed))
__attribute__((objc_final))
@interface ExecutionRouter {
@public
    uint32_t router_node_id;
}
- (bool)forwardReport:(ExecutionReport)report toChannel:(struct veriobjc_channel *)ch;
@end

@implementation ExecutionRouter
- (bool)forwardReport:(ExecutionReport)report toChannel:(struct veriobjc_channel *)ch {
    if (!ch) {
        return false;
    }
    return veriobjc_channel_send(ch, &report, sizeof(ExecutionReport));
}
@end

bool run_linear_channel_step(
    struct veriobjc_stack *stack,
    ExecutionRouter *router,
    struct veriobjc_channel *tx_channel,
    ExecutionReport *out_received
) {
    if (!stack || !router || !tx_channel || !out_received) {
        return false;
    }

    {
        VERIOBJC_SCOPED_STACK(stack);
        ExecutionReport *staged = (ExecutionReport *)veriobjc_stack_alloc(stack, sizeof(ExecutionReport));
        if (!staged) {
            return false;
        }
        staged->sequence_num = 42u;
        staged->limit_price  = 125000;
        staged->shares       = 500u;
        staged->symbol_id    = 7u;
        staged->side         = 1u;

        ExecutionReport owned_report = *staged;
        bool sent = [router forwardReport:veriobjc_move(owned_report) toChannel:tx_channel];
        if (!sent) {
            return false;
        }
    }

    return veriobjc_channel_recv(tx_channel, out_received, sizeof(ExecutionReport));
}
