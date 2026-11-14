#include "CppUTest/TestHarness.h"

extern "C" {
#include "veriobjc_runtime.h"
#include <vstd/string.h>
#include <vstd/span.h>
#include <vstd/strong.h>
#include <vstd/result.h>
#include <vstd/stack.h>
#include <sys0/mem.h>
#include <sys0/time.h>
#include <sys0/mmu.h>
#include <sys0/boot.h>
}

vspan_def(IntSpan, int32_t);
vfixed_string_def(FixedStr16, 16);
vstrong_def(TestPrice, int64_t);
vresult_def(ParseResult, uint32_t, int32_t);
vstack_array_def(IntStackArray, int32_t);

TEST_GROUP(VstdWave1AndSys0Group) {};

TEST(VstdWave1AndSys0Group, StringViewAndFixedStringOperations) {
    struct vstring_view empty = vstring_view_empty();
    LONGS_EQUAL(0u, empty.len);
    CHECK_TRUE(vstring_view_is_empty(empty));
    LONGS_EQUAL(0, vstring_view_at(empty, 0u));

    struct vstring_view null_view = vstring_view_init(nullptr, 5u);
    LONGS_EQUAL(0u, null_view.len);

    struct vstring_view lit = vstring_view_from_cstr("8=FIX.4.2|35=D|");
    LONGS_EQUAL(15u, lit.len);
    CHECK_FALSE(vstring_view_is_empty(lit));
    LONGS_EQUAL('8', vstring_view_at(lit, 0u));
    LONGS_EQUAL(0, vstring_view_at(lit, 99u));

    struct vstring_view prefix = vstring_view_FromLiteral("8=FIX.4.2");
    CHECK_TRUE(vstring_view_starts_with(lit, prefix));
    struct vstring_view bad_prefix = vstring_view_FromLiteral("9=FIX");
    CHECK_FALSE(vstring_view_starts_with(lit, bad_prefix));
    struct vstring_view long_prefix = vstring_view_FromLiteral("8=FIX.4.2|35=D|EXTRA");
    CHECK_FALSE(vstring_view_starts_with(lit, long_prefix));

    struct vstring_view sub = vstring_view_slice(lit, 10u, 4u);
    struct vstring_view expected_sub = vstring_view_FromLiteral("35=D");
    CHECK_TRUE(vstring_view_eq(sub, expected_sub));
    CHECK_FALSE(vstring_view_eq(sub, prefix));
    CHECK_FALSE(vstring_view_eq(sub, vstring_view_FromLiteral("35=X")));

    struct vstring_view oob_slice = vstring_view_slice(lit, 20u, 2u);
    LONGS_EQUAL(0u, oob_slice.len);
    struct vstring_view clamped_slice = vstring_view_slice(lit, 10u, 50u);
    LONGS_EQUAL(5u, clamped_slice.len);

    LONGS_EQUAL(0u, vstring_view_from_cstr(nullptr).len);
    struct vstring_view raw_null = { .data = nullptr, .len = 2u };
    LONGS_EQUAL('\0', vstring_view_at(raw_null, 0u));
    LONGS_EQUAL(0u, vstring_view_slice(raw_null, 0u, 1u).len);

    FixedStr16 fs;
    vfixed_string_init(&fs);
    vfixed_string_init((FixedStr16 *)nullptr);
    CHECK_FALSE(vfixed_string_copy((FixedStr16 *)nullptr, sub));
    CHECK_TRUE(vfixed_string_copy(&fs, sub));
    LONGS_EQUAL(4u, fs.len);
    CHECK_TRUE(vstring_view_eq(vfixed_string_view(&fs), expected_sub));
    LONGS_EQUAL(0u, vfixed_string_view((const FixedStr16 *)nullptr).len);

    struct vstring_view too_long = vstring_view_FromLiteral("0123456789ABCDEF_TOO_LONG");
    CHECK_FALSE(vfixed_string_copy(&fs, too_long));
}

TEST(VstdWave1AndSys0Group, ContiguousSpanOperations) {
    int32_t dma_words[4] = {10, 20, 30, 40};
    IntSpan sp = vspan_from_array(IntSpan, dma_words);
    LONGS_EQUAL(4u, sp.len);
    POINTERS_EQUAL(&dma_words[0], vspan_at(&sp, 0u));
    POINTERS_EQUAL(nullptr, vspan_at(&sp, 4u));
    POINTERS_EQUAL(nullptr, vspan_at((const IntSpan *)nullptr, 0u));

    IntSpan null_sp;
    vspan_init(&null_sp, (int32_t *)nullptr, 4u);
    LONGS_EQUAL(0u, null_sp.len);

    IntSpan init_sp;
    vspan_init(&init_sp, dma_words, 4u);
    LONGS_EQUAL(4u, init_sp.len);

    IntSpan sub = vspan_subspan(IntSpan, sp, 1u, 2u);
    LONGS_EQUAL(2u, sub.len);
    LONGS_EQUAL(20, *vspan_at(&sub, 0u));
    LONGS_EQUAL(30, *vspan_at(&sub, 1u));

    IntSpan bad_sub = vspan_subspan(IntSpan, sp, 5u, 1u);
    LONGS_EQUAL(0u, bad_sub.len);
    IntSpan trunc_sub = vspan_subspan(IntSpan, sp, 2u, 10u);
    LONGS_EQUAL(2u, trunc_sub.len);
}

TEST(VstdWave1AndSys0Group, StrongTypesAndBranchProvenResult) {
    TestPrice p1 = vstrong_make(TestPrice, 1050);
    TestPrice p2 = vstrong_make(TestPrice, 1050);
    TestPrice p3 = vstrong_make(TestPrice, 2000);
    LONGS_EQUAL(1050, vstrong_unwrap(p1));
    CHECK_TRUE(vstrong_eq(p1, p2));
    CHECK_FALSE(vstrong_eq(p1, p3));
    CHECK_TRUE(vstrong_lt(p1, p3));
    CHECK_FALSE(vstrong_lt(p3, p1));

    ParseResult ok_res = vresult_ok(ParseResult, 42u);
    CHECK_TRUE(vresult_is_ok(ok_res));
    LONGS_EQUAL(42u, vresult_unwrap_checked(ok_res));
    LONGS_EQUAL(0, vresult_unwrap_err_or(ok_res, 0));

    ParseResult err_res = vresult_err(ParseResult, -7);
    CHECK_FALSE(vresult_is_ok(err_res));
    LONGS_EQUAL(99u, vresult_unwrap_or(err_res, 99u));
    LONGS_EQUAL(42u, vresult_unwrap_or(ok_res, 99u));
    LONGS_EQUAL(-7, vresult_unwrap_err_or(err_res, 0));
}

TEST(VstdWave1AndSys0Group, StackArrayReferenceStabilityAndScopeReset) {
    alignas(64) uint8_t arena_buf[65536];
    struct veriobjc_stack st;
    veriobjc_stack_init(&st, arena_buf, sizeof(arena_buf));

    {
        VERIOBJC_SCOPED_STACK(&st);
        IntStackArray arr;
        vstack_array_init(&arr, &st);
        vstack_array_init((IntStackArray *)nullptr, &st);
        vstack_array_init(&arr, nullptr);
        vstack_array_init(&arr, &st);

        int32_t *first_ref = vstack_array_push(&arr, 777);
        CHECK_TRUE(first_ref != nullptr);
        LONGS_EQUAL(777, *first_ref);

        for (int32_t i = 1; i < 2048; ++i) {
            int32_t *slot = vstack_array_push(&arr, i);
            CHECK_TRUE(slot != nullptr);
        }
        // Reference captured before 2,047 subsequent pushes remains valid and writable!
        LONGS_EQUAL(777, *first_ref);
        *first_ref = 888;
        LONGS_EQUAL(888, *vstack_array_at(&arr, 0u));
        POINTERS_EQUAL(nullptr, vstack_array_at(&arr, 9999u));
        POINTERS_EQUAL(nullptr, vstack_array_at((const IntStackArray *)nullptr, 0u));
        POINTERS_EQUAL(nullptr, vstack_array_push((IntStackArray *)nullptr, 1));
    }
    // Single-store scope reset reclaimed all 2,048 elements
    LONGS_EQUAL(0u, veriobjc_stack_used(&st));

    // Exhausted stack returns nullptr on push
    struct veriobjc_stack tiny_st;
    uint8_t tiny_buf[8];
    veriobjc_stack_init(&tiny_st, tiny_buf, 0u);
    IntStackArray tiny_arr;
    vstack_array_init(&tiny_arr, &tiny_st);
    POINTERS_EQUAL(nullptr, vstack_array_push(&tiny_arr, 1));
}

TEST(VstdWave1AndSys0Group, Sys0MemoryTimeMmuAndBootPrimitives) {
    uint8_t src[37];
    uint8_t dst[37];
    uint8_t nt_dst[37];
    for (size_t i = 0; i < sizeof(src); ++i) {
        src[i] = (uint8_t)(i * 3u + 1u);
    }

    sys0_memset(dst, 0xAA, sizeof(dst));
    LONGS_EQUAL(0xAA, dst[0]);
    LONGS_EQUAL(0xAA, dst[36]);
    sys0_memset(nullptr, 0, 10u);

    sys0_memcpy(dst, src, sizeof(src));
    sys0_memcpy(nullptr, src, 4u);
    sys0_memcpy(dst, nullptr, 4u);
    LONGS_EQUAL(0, sys0_memcmp(dst, src, sizeof(src)));

    sys0_memcpy_nt(nt_dst, src, sizeof(src));
    sys0_memcpy_nt(nullptr, src, 4u);
    sys0_memcpy_nt(nt_dst, nullptr, 4u);
    LONGS_EQUAL(0, sys0_memcmp(nt_dst, src, sizeof(src)));

    nt_dst[18] ^= 0xFFu;
    CHECK_TRUE(sys0_memcmp(src, nt_dst, sizeof(src)) != 0);
    LONGS_EQUAL(0, sys0_memcmp(nullptr, src, 0u));
    LONGS_EQUAL(0, sys0_memcmp(nullptr, src, 4u));
    LONGS_EQUAL(0, sys0_memcmp(src, nullptr, 4u));

    uint64_t t0 = sys0_rdtsc();
    sys0_lfence();
    uint64_t t1 = sys0_rdtsc();
    CHECK_TRUE(t1 >= t0);

    alignas(4096) uint8_t page_mem[4096];
    sys0_wb_region_t wb = sys0_mmu_make_wb(page_mem, sizeof(page_mem));
    sys0_wc_region_t wc = sys0_mmu_make_wc(page_mem, sizeof(page_mem));
    sys0_dma_region_t dma = sys0_mmu_make_dma(page_mem, 0x100000ULL, sizeof(page_mem));

    LONGS_EQUAL(4096u, wb.size);
    LONGS_EQUAL(4096u, wc.size);
    LONGS_EQUAL(0x100000ULL, dma.phys_addr);
    CHECK_TRUE(sys0_mmu_is_page_aligned((uintptr_t)page_mem));
    CHECK_FALSE(sys0_mmu_is_page_aligned((uintptr_t)page_mem + 1u));

    uint64_t pte = sys0_mmu_encode_pte(0x200000ULL, SYS0_PTE_PRESENT | SYS0_PTE_WRITABLE | SYS0_PTE_HUGE_2MB);
    CHECK_TRUE((pte & SYS0_PTE_HUGE_2MB) != 0ULL);

    struct sys0_core_boot_info boot;
    sys0_boot_init(&boot, 2u, 0u, page_mem, sizeof(page_mem));
    sys0_boot_init(nullptr, 0u, 0u, nullptr, 0u);
    LONGS_EQUAL(2u, boot.core_id);
    POINTERS_EQUAL(page_mem, sys0_tls_base(&boot));
    POINTERS_EQUAL(nullptr, sys0_tls_base(nullptr));
}
