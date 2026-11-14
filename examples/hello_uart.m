// examples/hello_uart.m
// Task 3.15 Bare-Metal Minimal-Binary Proof:
//   - Imports ONLY <vstd/io.h>, <sys0/boot.h>, and <hw/loopback.h>
//   - Zero container/math/ternary headers linked
#include "veriobjc_runtime.h"
#include "vstd/io.h"
#include "sys0/boot.h"
#include "hw/loopback.h"

int32_t hello_uart_baremetal_entry(void) {
    uint8_t tls_mem[64] __attribute__((aligned(16)));
    struct sys0_core_boot_info core0;
    sys0_boot_init(&core0, 0u, 0u, tls_mem, sizeof(tls_mem));

    struct hw_uart_loopback_dev uart;
    hw_uart_loopback_init(&uart);
    const char banner[5] = {'V', 'E', 'R', 'I', '\n'};
    if (!hw_uart_write_nonblock(&uart, banner, sizeof(banner))) {
        return -1;
    }
    char rx[8] = {0};
    size_t read_bytes = 0u;
    if (!hw_uart_read_nonblock(&uart, rx, sizeof(rx), &read_bytes)) {
        return -1;
    }
    return (read_bytes == 5u && rx[0] == 'V') ? 0 : 1;
}

#ifdef VERIOBJC_EXAMPLE_MAIN
int main(void) {
    return hello_uart_baremetal_entry();
}
#endif
