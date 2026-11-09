// Negative test: blocking POSIX I/O (read/write) is banned in @nolatency contexts
#include "veriobjc_runtime.h"

extern long read(int fd, void *buf, size_t count);

veriobjc_nolatency
long poll_network_socket(int fd, void *dma_ring) {
    return read(fd, dma_ring, 64);
}
