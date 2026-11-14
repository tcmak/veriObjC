#pragma once
#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <sys0/mmu.h> — MMU-As-A-Library: Distinct Caching & DMA Region Types
// Layer: sys0 (depends on: rt)
// ---------------------------------------------------------------------------

#define SYS0_PAGE_SIZE_4KB   4096ULL
#define SYS0_PAGE_SIZE_2MB   (2ULL * 1024ULL * 1024ULL)

#define SYS0_PTE_PRESENT       (1ULL << 0)
#define SYS0_PTE_WRITABLE      (1ULL << 1)
#define SYS0_PTE_WRITE_THROUGH (1ULL << 3)
#define SYS0_PTE_CACHE_DISABLE (1ULL << 4)
#define SYS0_PTE_HUGE_2MB      (1ULL << 7)
#define SYS0_PTE_WRITE_COMBINE (SYS0_PTE_WRITE_THROUGH | SYS0_PTE_CACHE_DISABLE)

// Strictly distinct nominal types for Write-Back, Write-Combining, and DMA
typedef struct {
    void  *virt_base;
    size_t size;
} sys0_wb_region_t;

typedef struct {
    void  *virt_base;
    size_t size;
} sys0_wc_region_t;

typedef struct {
    void    *virt_base;
    uint64_t phys_addr;
    size_t   size;
} sys0_dma_region_t;

static inline sys0_wb_region_t sys0_mmu_make_wb(void *virt_base, size_t size) {
    sys0_wb_region_t r = { .virt_base = virt_base, .size = size };
    return r;
}

static inline sys0_wc_region_t sys0_mmu_make_wc(void *virt_base, size_t size) {
    sys0_wc_region_t r = { .virt_base = virt_base, .size = size };
    return r;
}

static inline sys0_dma_region_t sys0_mmu_make_dma(void *virt_base, uint64_t phys_addr, size_t size) {
    sys0_dma_region_t r = { .virt_base = virt_base, .phys_addr = phys_addr, .size = size };
    return r;
}

static inline bool sys0_mmu_is_page_aligned(uintptr_t addr) {
    return (addr & (SYS0_PAGE_SIZE_4KB - 1ULL)) == 0ULL;
}

static inline uint64_t sys0_mmu_encode_pte(uint64_t phys_addr, uint64_t flags) {
    return (phys_addr & ~(SYS0_PAGE_SIZE_4KB - 1ULL)) | flags;
}

#ifdef __cplusplus
}
#endif
