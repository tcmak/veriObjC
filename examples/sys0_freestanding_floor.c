// ===========================================================================
// VeriObjC Phase 3 (Task 3.10): sys0/ Freestanding Floor
// Arch-tuned memory ops, non-temporal stores, rdtsc clock, MMU & per-core boot
// ===========================================================================

#include <sys0/mem.h>
#include <sys0/time.h>
#include <sys0/mmu.h>
#include <sys0/boot.h>

uint64_t sys0_freestanding_self_test(void *dma_page, uint64_t phys_addr) {
    uint8_t src_line[64];
    uint8_t dst_line[64];

    sys0_memset(src_line, 0x5A, sizeof(src_line));
    sys0_memcpy_nt(dst_line, src_line, sizeof(src_line));
    if (sys0_memcmp(src_line, dst_line, sizeof(src_line)) != 0) {
        return 0ULL;
    }

    uint64_t t_start = sys0_rdtsc();
    sys0_lfence();

    sys0_dma_region_t dma = sys0_mmu_make_dma(dma_page, phys_addr, SYS0_PAGE_SIZE_4KB);
    uint64_t pte = sys0_mmu_encode_pte(dma.phys_addr, SYS0_PTE_PRESENT | SYS0_PTE_WRITABLE | SYS0_PTE_WRITE_COMBINE);

    struct sys0_core_boot_info core0;
    sys0_boot_init(&core0, 0u, 0u, dma_page, SYS0_PAGE_SIZE_4KB);

    uint64_t t_end = sys0_rdtsc();
    return (t_end - t_start) + pte + (uint64_t)core0.core_id;
}
