// Valid stock C/Objective-C: calling POSIX read() inside a hot loop.
// Rejected by VeriObjC: blocking POSIX I/O is banned in @nolatency contexts.
#include <stddef.h>

extern long read(int fd, void *buf, size_t count);

__attribute__((annotate("objc_nolatency")))
long valid_objc_blocking_read(int fd, void *buf) {
    return read(fd, buf, 64);
}
