// Negative test: allocator mixing (calling C heap free() on VeriObjC memory) is banned
#include "veriobjc_runtime.h"

extern void free(void *ptr);

void mix_allocators(void *arena_ptr) {
    free(arena_ptr);
}
