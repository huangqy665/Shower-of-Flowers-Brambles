#pragma once

#include <algorithm>
#include <vector>

namespace dillen::kernel {

// Secondary indexes ("all entities of type T", "all instances of definition D")
// are kept sorted ascending by the id they hold.
//
// This is not a micro-optimisation, it is a determinism contract. Every id in
// this engine is a hash (StableEntityId, StableMechanismInstanceId, ...), so
// creation order and id order are unrelated. The Query Snapshots used to
// rebuild these indexes by walking the primary map -- which yields id order --
// while the stores maintained them by push_back -- which yields creation order.
// The two views therefore disagreed, harmlessly only because nothing read the
// store-side order. Now that a snapshot shares the store's payload instead of
// rebuilding it, one order has to win, and it must be the one gameplay already
// observes through the snapshot: ascending id.
//
// The trade-off is deliberate: because ids are hashes the insertion point is
// random, so this is O(k) memmove of 8-byte values per insert rather than the
// O(1) push_back it replaces. These indexes are read by iteration on the
// gameplay path and written only when an Entity/Relation/Instance is created,
// so a std::set (O(log k) insert, allocation per element, pointer-chasing
// reads) would trade the hot path for the cold one. Measured cost of the
// change: building a world of 16000 instances of one definition went from
// 5.6 ms to 19.9 ms, once, at load. Bulk restore does not pay it at all --
// runtime_persistence.cpp rebuilds these by walking the primary map, which is
// already ascending, so push_back is correct there.
template <class Id>
void InsertSortedId(std::vector<Id>& index, Id id)
{
    index.insert(std::upper_bound(index.begin(), index.end(), id), id);
}

}
