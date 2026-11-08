// Negative test: freeing a pool handle or slot twice is banned at compile time
#include "veriobjc_runtime.h"

void test_double_free(struct veriobjc_gen_pool *pool) {
    struct veriobjc_handle h = veriobjc_gen_pool_alloc(pool);
    veriobjc_gen_pool_free(pool, h);

    // Violation: double free of handle 'h'
    veriobjc_gen_pool_free(pool, h);
}
