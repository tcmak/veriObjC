#include "veriobjc_runtime.h"

typedef struct {
    uint16_t magic;
    uint16_t payload_len;
    uint32_t seq_num;
    uint32_t crc;
} WirePacketHeader;

static int32_t verify_packet_crc(const WirePacketHeader *hdr) {
    if (hdr->crc == 0u) {
        veriobjc_return_err_from(-101, parse_wire_packet);
    }
    return 0;
}

static int32_t decode_packet_bounds(const WirePacketHeader *hdr) {
    if (hdr->magic != 0x564Fu) {
        veriobjc_return_err_from(-100, parse_wire_packet);
    }
    if (hdr->payload_len > 1500u) {
        veriobjc_return_err_from(-102, parse_wire_packet);
    }
    int32_t crc_status = verify_packet_crc(hdr);
    if (crc_status < 0) {
        return crc_status;
    }
    return (int32_t)hdr->payload_len;
}

int32_t parse_wire_packet(const WirePacketHeader *hdr) {
    if (!hdr) {
        return -1;
    }
    return decode_packet_bounds(hdr);
}
