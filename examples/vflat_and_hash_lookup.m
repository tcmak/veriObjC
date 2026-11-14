// examples/vflat_and_hash_lookup.m
// Demonstrates Task 3.4 (<vstd/map.h>, <vstd/hash.h>):
//   - Sorted parallel-array vflat_map with deterministic binary search
//   - Power-of-two open-addressing vhash_flat with 75% load-factor ceiling
#include "veriobjc_runtime.h"
#include "vstd/map.h"
#include "vstd/hash.h"

vflat_map_def(InstrumentFlatMap4, uint64_t, int32_t, 4);
vhash_flat_def(OrderFastHash8, uint64_t, int32_t, 8);

int32_t run_maps_lookup_demo(void) {
    InstrumentFlatMap4 instruments;
    vflat_map_init(&instruments);
    vflat_map_insert_u64(&instruments, 200u, 20);
    vflat_map_insert_u64(&instruments, 100u, 10);

    OrderFastHash8 orders;
    vhash_flat_init(&orders);
    vhash_flat_insert_u64(&orders, 9001u, 75);

    int32_t *tick_size = vflat_map_find_u64(&instruments, 100u);
    int32_t *order_qty = vhash_flat_find_u64(&orders, 9001u);
    return (tick_size && order_qty) ? (*tick_size + *order_qty) : 0;
}

#ifdef VERIOBJC_EXAMPLE_MAIN
int main(void) {
    return (run_maps_lookup_demo() == 85) ? 0 : 1;
}
#endif
