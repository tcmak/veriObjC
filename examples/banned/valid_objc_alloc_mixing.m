// valid_objc_alloc_mixing.m
// Valid stock Objective-C/C. Banned in VeriObjC: allocator mixing (foreign free).
// Replacement: Scoped data stacks and generational vpool handles.

extern void free(void *ptr);

void release_foreign_buffer(void *buf) {
    free(buf);
}
