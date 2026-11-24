#include <CppUTest/TestHarness.h>
#include <cstdio>
#include <cstring>
#include <unistd.h>

#include "veriobjc_host_io.h"
#include "vstd/span.h"

vspan_def(HostConstByteSpan, const uint8_t);

TEST_GROUP(VeriObjCHostIoGroup) {
    char tmp_valid[128];
    char tmp_empty[128];
    char tmp_large[128];

    void setup() override {
        std::snprintf(tmp_valid, sizeof(tmp_valid), "/tmp/veriobjc_host_io_valid_%d.bin", (int)getpid());
        std::snprintf(tmp_empty, sizeof(tmp_empty), "/tmp/veriobjc_host_io_empty_%d.bin", (int)getpid());
        std::snprintf(tmp_large, sizeof(tmp_large), "/tmp/veriobjc_host_io_large_%d.bin", (int)getpid());

        FILE *f1 = std::fopen(tmp_valid, "wb");
        CHECK(f1 != nullptr);
        const uint8_t payload[8] = {0x56u, 0x4Fu, 0x42u, 0x4Au, 0x01u, 0x02u, 0x03u, 0x04u};
        LONGS_EQUAL(8u, std::fwrite(payload, 1u, sizeof(payload), f1));
        std::fclose(f1);

        FILE *f2 = std::fopen(tmp_empty, "wb");
        CHECK(f2 != nullptr);
        std::fclose(f2);

        FILE *f3 = std::fopen(tmp_large, "wb");
        CHECK(f3 != nullptr);
        uint8_t big[40];
        std::memset(big, 0xAB, sizeof(big));
        LONGS_EQUAL(sizeof(big), std::fwrite(big, 1u, sizeof(big), f3));
        std::fclose(f3);
    }

    void teardown() override {
        std::remove(tmp_valid);
        std::remove(tmp_empty);
        std::remove(tmp_large);
    }
};

TEST(VeriObjCHostIoGroup, LoadsExactBytesIntoStaticStagingAndConvertsToVSpan) {
    static uint8_t g_staging[16];
    std::memset(g_staging, 0, sizeof(g_staging));

    struct veriobjc_host_io_result res =
        veriobjc_host_load_file_staged(tmp_valid, g_staging, sizeof(g_staging));
    CHECK_TRUE(res.is_ok);
    LONGS_EQUAL(VERIOBJC_HOST_IO_OK, res.err);
    LONGS_EQUAL(8u, res.value.len);
    POINTERS_EQUAL(g_staging, res.value.data);
    LONGS_EQUAL(0x56u, res.value.data[0]);
    LONGS_EQUAL(0x04u, res.value.data[7]);

    HostConstByteSpan sp = veriobjc_host_slice_as_span(HostConstByteSpan, res.value);
    LONGS_EQUAL(8u, sp.len);
    const uint8_t *last = vspan_at(&sp, 7u);
    CHECK(last != nullptr);
    LONGS_EQUAL(0x04u, *last);

    // Exact capacity boundary (8-byte file into 8-byte staging buffer) must also succeed
    static uint8_t g_exact[8];
    struct veriobjc_host_io_result exact_res =
        veriobjc_host_load_file_staged(tmp_valid, g_exact, sizeof(g_exact));
    CHECK_TRUE(exact_res.is_ok);
    LONGS_EQUAL(8u, exact_res.value.len);
}

TEST(VeriObjCHostIoGroup, FailsClosedOnInvalidMissingEmptyAndOversizedFiles) {
    static uint8_t g_staging[16];

    // 1. Invalid arguments
    struct veriobjc_host_io_result r_null_path =
        veriobjc_host_load_file_staged(nullptr, g_staging, sizeof(g_staging));
    CHECK_FALSE(r_null_path.is_ok);
    LONGS_EQUAL(VERIOBJC_HOST_IO_ERR_INVALID_ARG, r_null_path.err);

    struct veriobjc_host_io_result r_empty_path =
        veriobjc_host_load_file_staged("", g_staging, sizeof(g_staging));
    CHECK_FALSE(r_empty_path.is_ok);
    LONGS_EQUAL(VERIOBJC_HOST_IO_ERR_INVALID_ARG, r_empty_path.err);

    struct veriobjc_host_io_result r_null_buf =
        veriobjc_host_load_file_staged(tmp_valid, nullptr, sizeof(g_staging));
    CHECK_FALSE(r_null_buf.is_ok);
    LONGS_EQUAL(VERIOBJC_HOST_IO_ERR_INVALID_ARG, r_null_buf.err);

    struct veriobjc_host_io_result r_zero_cap =
        veriobjc_host_load_file_staged(tmp_valid, g_staging, 0u);
    CHECK_FALSE(r_zero_cap.is_ok);
    LONGS_EQUAL(VERIOBJC_HOST_IO_ERR_INVALID_ARG, r_zero_cap.err);

    // 2. Missing file
    struct veriobjc_host_io_result r_missing =
        veriobjc_host_load_file_staged("/tmp/veriobjc_nonexistent_file_605.bin", g_staging, sizeof(g_staging));
    CHECK_FALSE(r_missing.is_ok);
    LONGS_EQUAL(VERIOBJC_HOST_IO_ERR_OPEN, r_missing.err);

    // 3. Empty (0-byte) file
    struct veriobjc_host_io_result r_empty =
        veriobjc_host_load_file_staged(tmp_empty, g_staging, sizeof(g_staging));
    CHECK_FALSE(r_empty.is_ok);
    LONGS_EQUAL(VERIOBJC_HOST_IO_ERR_EMPTY, r_empty.err);

    // 4. Oversized file (40 bytes > 16-byte staging buffer)
    struct veriobjc_host_io_result r_oversized =
        veriobjc_host_load_file_staged(tmp_large, g_staging, sizeof(g_staging));
    CHECK_FALSE(r_oversized.is_ok);
    LONGS_EQUAL(VERIOBJC_HOST_IO_ERR_TOO_LARGE, r_oversized.err);
}
