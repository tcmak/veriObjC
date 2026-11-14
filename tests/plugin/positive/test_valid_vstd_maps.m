#include "veriobjc_runtime.h"
#include "vstd/map.h"
#include "vstd/hash.h"

vflat_map_def(VenueMap4, uint64_t, int32_t, 4);
vhash_flat_def(OrderHash8, uint64_t, int32_t, 8);

int32_t run_valid_vstd_maps(void) {
    VenueMap4 vmap;
    vflat_map_init(&vmap);
    vflat_map_insert_u64(&vmap, 1001u, 15);

    OrderHash8 hmap;
    vhash_flat_init(&hmap);
    vhash_flat_insert_u64(&hmap, 1001u, 25);

    int32_t *a = vflat_map_find_u64(&vmap, 1001u);
    int32_t *b = vhash_flat_find_u64(&hmap, 1001u);
    return (a && b) ? (*a + *b) : 0;
}
