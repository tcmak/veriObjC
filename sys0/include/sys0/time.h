#pragma once
#include "veriobjc_runtime.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// <sys0/time.h> — Bare-Metal Hardware Cycle Counter (rdtsc / CNTVCT_EL0)
// Layer: sys0 (depends on: rt)
// ---------------------------------------------------------------------------

static inline uint64_t sys0_rdtsc(void) {
#if defined(__x86_64__) || defined(__i386__)
    uint32_t lo = 0u;
    uint32_t hi = 0u;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32u) | (uint64_t)lo;
#elif defined(__aarch64__)
    uint64_t val = 0ULL;
    __asm__ __volatile__("mrs %0, cntvct_el0" : "=r"(val));
    return val;
#else
    return 0ULL;
#endif
}

static inline void sys0_lfence(void) {
#if defined(__x86_64__) || defined(__i386__)
    __asm__ __volatile__("lfence" ::: "memory");
#else
    __sync_synchronize();
#endif
}

#ifdef __cplusplus
}
#endif
