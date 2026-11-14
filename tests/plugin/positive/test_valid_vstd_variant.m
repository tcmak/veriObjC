#include "veriobjc_runtime.h"
#include "vstd/variant.h"

typedef enum : uint32_t {
    VENUE_ACK = 0,
    VENUE_REJECT = 1
} VenueReplyTag;

vvariant_def(VenueReply, VenueReplyTag, {
    int32_t ack_seq;
    int32_t reject_reason;
});

int32_t handle_venue_reply(VenueReply reply) {
    vmatch(reply) {
        case VENUE_ACK:
            return reply.payload.ack_seq;
        case VENUE_REJECT:
            return -reply.payload.reject_reason;
    }
    return 0;
}
