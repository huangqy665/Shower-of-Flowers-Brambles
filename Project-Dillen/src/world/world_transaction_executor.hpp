#pragma once

#include <cstdint>

#include "authoritative_world.hpp"
#include "frozen_runtime_catalog.hpp"
#include "world_transaction.hpp"

namespace dillen::world {

class WorldTransactionExecutor
{
public:
    kernel::WorldTransactionResult Apply(
        AuthoritativeWorld& world,
        const kernel::WorldTransaction& transaction,
        const kernel::FrozenRuntimeCatalog& catalog,
        std::uint64_t currentTick
    ) const;
};

// Applies many transactions against ONE copy of the authoritative stores.
//
// `WorldTransactionExecutor::Apply` copies all six stores per call so a
// rejected transaction leaves no half-update. That is correct but costs
// O(world) per transaction, and the Kernel Runtime commits one transaction per
// algorithm invocation -- so a tick costs O(instances) x O(world) (memo
// section 3.20). A batch copies once, applies each transaction to the working
// copies in order, and moves them back only on Commit().
//
// Atomicity is preserved by the caller's usage contract, not weakened:
// - each Apply() still either fully applies or leaves the working copies
//   untouched, exactly as the single-transaction path does;
// - if the caller abandons the batch (no Commit), the authoritative world is
//   untouched by every transaction in it.
// The Kernel Runtime uses this optimistically and falls back to the
// per-transaction path for the whole phase the moment anything is rejected, so
// rejection behaviour stays byte-identical to the unbatched implementation.
class WorldTransactionBatch
{
public:
    WorldTransactionBatch(
        AuthoritativeWorld& world,
        const kernel::FrozenRuntimeCatalog& catalog,
        std::uint64_t currentTick
    );

    // True when the batch may be used at all. False for a non-frozen catalog
    // or a tick regression -- the caller must then use the single-transaction
    // path, which reports the specific status.
    bool IsOpen() const noexcept;

    // Working-copy view, reflecting every transaction applied so far. The
    // Kernel Runtime reads this instead of a per-transaction republished Query
    // Snapshot when deciding whether an invocation target is still available.
    const kernel::MechanismInstanceStore& Mechanisms() const noexcept;

    kernel::WorldTransactionResult Apply(
        const kernel::WorldTransaction& transaction
    );

    // Moves the working copies into the authoritative world. After this the
    // batch is closed. Not calling it discards every applied transaction.
    void Commit();

private:
    AuthoritativeWorld& world_;
    const kernel::FrozenRuntimeCatalog& catalog_;
    std::uint64_t currentTick_ = 0;
    bool open_ = false;
    EntityRegistry entities_;
    ComponentStore components_;
    RelationIndex relations_;
    kernel::MechanismInstanceStore mechanisms_;
    kernel::AlgorithmInbox algorithmInbox_;
    kernel::DeterministicRngRegistry rngStreams_;
};

}
