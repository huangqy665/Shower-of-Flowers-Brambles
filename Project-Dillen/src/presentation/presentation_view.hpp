#pragma once

#include <cstdint>

#include "world_query_snapshot.hpp"

namespace dillen::presentation {

// The only door from the authoritative world into anything that draws.
//
// Everything on the presentation side -- map renderer, mechanism panels, a GUI
// shell -- reads through this and nothing else. The type exists to make the
// direction structural rather than a convention:
//
//   * it holds a WorldQuerySnapshotHandle, which is
//     shared_ptr<const WorldQuerySnapshot>, so there is no non-const path to
//     the authoritative stores at all; and
//   * it owns that handle, so a frame can keep reading a published snapshot
//     while the Kernel Runtime is already ticking past it. The stores are
//     copy-on-write, so holding one is a refcount, not a copy of the world.
//
// The rule this encodes is the one that keeps presentation deletable: a
// renderer is a pure function of a published snapshot. It never writes, never
// blocks the tick, and never becomes something the simulation reads back.
// `src/presentation` may include kernel, world and runtime; nothing may
// include `src/presentation` (architecture_guard_probe enforces both).
//
// Deliberately thin at Demo 0.8 P0. The read model a map actually needs -- a
// compact per-province attribute table with incremental update -- is P2 work,
// and it is designed against real map data rather than guessed at here.
class PresentationView
{
public:
    PresentationView() = default;
    explicit PresentationView(runtime::WorldQuerySnapshotHandle snapshot);

    // False before the first Publish, and after Reset.
    bool IsBound() const noexcept;

    // Undefined unless IsBound(). Const all the way down.
    const runtime::WorldQuerySnapshot& World() const noexcept;
    runtime::WorldQueryStamp Stamp() const noexcept;

    // Swaps in a newer published snapshot. Returns false and keeps the current
    // one when `snapshot` is null, unpublished, or older than what is already
    // held -- a frame must never step backwards in world time because a
    // publication arrived out of order.
    bool Advance(runtime::WorldQuerySnapshotHandle snapshot);

    void Reset() noexcept;

private:
    runtime::WorldQuerySnapshotHandle snapshot_;
};

}
