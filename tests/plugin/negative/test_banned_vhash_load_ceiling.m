// EXPECTED_DIAGNOSTIC: VeriObjC Violation: vhash_flat load-factor ceiling exceeded
#include "veriobjc_runtime.h"
#include "vstd/hash.h"

vhash_flat_def(TinyHash4, uint64_t, int32_t, 4);

void exceed_hash_load_ceiling(void) {
    TinyHash4 hmap;
    vhash_flat_init(&hmap);
    // Capacity 4 has a 75% load-factor ceiling of 3 insertions; 4th insert fails at compile time
    vhash_flat_insert_u64(&hmap, 1u, 10);
    vhash_flat_insert_u64(&hmap, 2u, 20);
    vhash_flat_insert_u64(&hmap, 3u, 30);
    vhash_flat_insert_u64(&hmap, 4u, 40);
}
