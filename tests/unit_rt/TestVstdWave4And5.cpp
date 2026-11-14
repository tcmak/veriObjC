#include "CppUTest/TestHarness.h"
#include "veriobjc_runtime.h"
#include "vstd/io.h"
#include "vstd/execution.h"
#include "vstd/simd.h"
#include "vstd/contracts.h"
#include "hw/loopback.h"

vdma_ring_def(TestDmaRing4, 16, 4);
vasync_task_def(IrqPacketTask, int32_t);

static int32_t double_step_fn(int32_t x) {
    return x * 2;
}

static int32_t add_five_step_fn(int32_t x) {
    return x + 5;
}

TEST_GROUP(VstdWave4And5Group) {
};

TEST(VstdWave4And5Group, NonBlockingDmaRingAndLoopbackTransport) {
    TestDmaRing4 ring;
    vdma_ring_init(&ring);
    vdma_ring_init((TestDmaRing4 *)nullptr);

    const uint8_t msg1[4] = {0xAA, 0xBB, 0xCC, 0xDD};
    uint8_t rx_buf[16] = {0};
    uint32_t rx_len = 0u;

    CHECK_FALSE(vdma_ring_submit((TestDmaRing4 *)nullptr, msg1, 4u));
    CHECK_FALSE(vdma_ring_submit(&ring, nullptr, 4u));
    // Oversized DMA line (> LineSize 16) rejected
    CHECK_FALSE(vdma_ring_submit(&ring, msg1, 20u));

    CHECK_FALSE(vdma_ring_poll(&ring, rx_buf, sizeof(rx_buf), &rx_len));
    CHECK_FALSE(vdma_ring_poll((TestDmaRing4 *)nullptr, rx_buf, sizeof(rx_buf), &rx_len));
    CHECK_FALSE(vdma_ring_poll(&ring, nullptr, sizeof(rx_buf), &rx_len));
    CHECK_FALSE(vdma_ring_poll(&ring, rx_buf, sizeof(rx_buf), nullptr));

    CHECK_TRUE(vdma_ring_submit(&ring, msg1, 4u));
    CHECK_TRUE(vdma_ring_submit(&ring, msg1, 4u));
    CHECK_TRUE(vdma_ring_submit(&ring, msg1, 4u));
    CHECK_TRUE(vdma_ring_submit(&ring, msg1, 4u));
    // Ring full (cap = 4)
    CHECK_FALSE(vdma_ring_submit(&ring, msg1, 4u));

    // Poll with buffer too small for 4-byte frame
    CHECK_FALSE(vdma_ring_poll(&ring, rx_buf, 2u, &rx_len));
    CHECK_TRUE(vdma_ring_poll(&ring, rx_buf, sizeof(rx_buf), &rx_len));
    LONGS_EQUAL(4u, rx_len);
    LONGS_EQUAL(0xAA, rx_buf[0]);
    LONGS_EQUAL(0xDD, rx_buf[3]);

    // Drain remaining 3 frames
    CHECK_TRUE(vdma_ring_poll(&ring, rx_buf, sizeof(rx_buf), &rx_len));
    CHECK_TRUE(vdma_ring_poll(&ring, rx_buf, sizeof(rx_buf), &rx_len));
    CHECK_TRUE(vdma_ring_poll(&ring, rx_buf, sizeof(rx_buf), &rx_len));

    // Loopback roundtrip over fresh ring
    uint8_t roundtrip_rx[16] = {0};
    uint32_t roundtrip_len = 0u;
    CHECK_TRUE(vio_loopback_roundtrip(&ring, msg1, 4u, roundtrip_rx, sizeof(roundtrip_rx), &roundtrip_len));
    LONGS_EQUAL(4u, roundtrip_len);
    LONGS_EQUAL(0xBB, roundtrip_rx[1]);
    CHECK_FALSE(vio_loopback_roundtrip((TestDmaRing4 *)nullptr, msg1, 4u, roundtrip_rx, sizeof(roundtrip_rx), &roundtrip_len));

    // Hardware UART loopback driver-as-library
    struct hw_uart_loopback_dev uart;
    hw_uart_loopback_init(&uart);
    hw_uart_loopback_init(nullptr);
    CHECK_FALSE(hw_uart_write_nonblock(nullptr, msg1, 4u));
    CHECK_FALSE(hw_uart_write_nonblock(&uart, nullptr, 4u));
    CHECK_FALSE(hw_uart_write_nonblock(&uart, msg1, 100u));
    CHECK_TRUE(hw_uart_write_nonblock(&uart, msg1, 4u));

    uint8_t uart_rx[8] = {0};
    size_t uart_read = 0u;
    CHECK_FALSE(hw_uart_read_nonblock(nullptr, uart_rx, sizeof(uart_rx), &uart_read));
    CHECK_FALSE(hw_uart_read_nonblock(&uart, nullptr, sizeof(uart_rx), &uart_read));
    CHECK_FALSE(hw_uart_read_nonblock(&uart, uart_rx, sizeof(uart_rx), nullptr));
    CHECK_FALSE(hw_uart_read_nonblock(&uart, uart_rx, 2u, &uart_read));
    CHECK_TRUE(hw_uart_read_nonblock(&uart, uart_rx, sizeof(uart_rx), &uart_read));
    LONGS_EQUAL(4u, uart_read);
    LONGS_EQUAL(0xCC, uart_rx[2]);
    // Second read when empty returns false
    CHECK_FALSE(hw_uart_read_nonblock(&uart, uart_rx, sizeof(uart_rx), &uart_read));
}

TEST(VstdWave4And5Group, LazyExecutionPipelineAndIrqWokenVasync) {
    struct vsender_i32 s0 = vsender_just_i32(18);
    struct vsender_i32 s1 = vsender_then_i32(s0, double_step_fn);
    struct vsender_i32 s2 = vsender_then_i32(s1, add_five_step_fn);
    struct vsender_i32 s_null = vsender_then_i32(s2, nullptr);
    LONGS_EQUAL(41, vsender_sync_wait_i32(s_null));

    IrqPacketTask task;
    vasync_task_init(&task, 99u);
    vasync_task_init((IrqPacketTask *)nullptr, 99u);
    CHECK_FALSE(vasync_task_is_ready(&task));
    CHECK_FALSE(vasync_task_is_ready((const IrqPacketTask *)nullptr));

    vasync_irq_wake((IrqPacketTask *)nullptr, 77);
    vasync_irq_wake(&task, 77);
    CHECK_TRUE(vasync_task_is_ready(&task));
    LONGS_EQUAL(77, task.result_val);
}

TEST(VstdWave4And5Group, HardwareAwareSimdMatchesScalarByteForByte) {
    int32_t a_raw[4] = {10, -20, 30, 40};
    int32_t b_raw[4] = {2, 3, 4, 5};
    vsimd_i32x4 va = vsimd_load_i32x4(a_raw);
    vsimd_i32x4 vzero = vsimd_load_i32x4(nullptr);
    LONGS_EQUAL(0, vzero.lanes[0]);

    vsimd_i32x4 vb = vsimd_load_i32x4(b_raw);
    vsimd_i32x4 vsum = vsimd_add_i32x4(va, vb);
    vsimd_i32x4 vprod = vsimd_mul_i32x4(va, vb);
    vsimd_i32x4 vsel = vsimd_select_gt_i32x4(va, vzero, va, vb);

    int32_t out_sum[4] = {0};
    int32_t out_prod[4] = {0};
    vsimd_store_i32x4(out_sum, vsum);
    vsimd_store_i32x4(nullptr, vsum);
    vsimd_store_i32x4(out_prod, vprod);

    for (int i = 0; i < 4; ++i) {
        LONGS_EQUAL(a_raw[i] + b_raw[i], out_sum[i]);
        LONGS_EQUAL(a_raw[i] * b_raw[i], out_prod[i]);
    }
    LONGS_EQUAL(10, vsel.lanes[0]);
    LONGS_EQUAL(3, vsel.lanes[1]);
    LONGS_EQUAL(74, vsimd_reduce_add_i32x4(vsum));
}

TEST(VstdWave4And5Group, PureContractsPredicatesAndSmtHelpers) {
    CHECK_TRUE(vcontract_in_range_i64(50, 0, 100));
    CHECK_FALSE(vcontract_in_range_i64(-1, 0, 100));
    CHECK_FALSE(vcontract_in_range_i64(101, 0, 100));

    int32_t dummy = 1;
    CHECK_TRUE(vcontract_non_null(&dummy));
    CHECK_FALSE(vcontract_non_null(nullptr));

    CHECK_TRUE(vcontract_is_pow2_u64(64u));
    CHECK_FALSE(vcontract_is_pow2_u64(0u));
    CHECK_FALSE(vcontract_is_pow2_u64(63u));

    alignas(64) uint8_t aligned_buf[64];
    CHECK_TRUE(vcontract_aligned_ptr(aligned_buf, 64u));
    CHECK_FALSE(vcontract_aligned_ptr(aligned_buf + 1, 64u));
    CHECK_FALSE(vcontract_aligned_ptr(nullptr, 64u));

    CHECK_TRUE(vcontract_implies(false, false));
    CHECK_TRUE(vcontract_implies(true, true));
    CHECK_FALSE(vcontract_implies(true, false));

    CHECK_TRUE(vcontract_ensures_eq_i64(42, 42));
    CHECK_FALSE(vcontract_ensures_eq_i64(42, 43));
}
