// Positive test: nominal structures and non-id variables compile cleanly
struct AudioPacket {
    unsigned int packet_id;
    float sample;
};

void test_valid_packet(struct AudioPacket *pkt) {
    if (pkt) {
        pkt->packet_id = 1;
        pkt->sample = 0.5f;
    }
}
