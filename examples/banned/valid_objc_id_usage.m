// valid_objc_id_usage.m
// Valid stock Objective-C. Banned in VeriObjC: generic 'id' type.
// Replacement: Sealed protocols or concrete nominal types.

id select_primary_object(id first, id second, int pick_first) {
    return pick_first ? first : second;
}
