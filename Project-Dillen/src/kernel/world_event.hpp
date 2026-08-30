#pragma once

#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

#include "mechanism_change.hpp"
#include "world_transaction.hpp"

namespace dillen::persistence {
class RuntimePersistenceService;
}

namespace dillen::kernel {

struct WorldTransactionCommittedEvent
{
    std::size_t changedInstances = 0;
    std::size_t changedObjects = 0;
};

struct WorldTransactionRejectedEvent
{
    WorldTransactionStatus status =
        WorldTransactionStatus::MechanismRejected;
    MechanismTransactionStatus mechanismStatus =
        MechanismTransactionStatus::Committed;
    std::size_t commandIndex = 0;
    std::uint64_t subject = 0;
    MechanismInstanceId target;
};

// FROZEN ORDER (Demo 0.2). Each alternative's position is written straight out
// as the Fact Stream payload tag, which the deterministic replay checksum is
// taken over -- so reordering or inserting anywhere but the END shifts every
// replay checksum even though these bytes are never read back.
// runtime_save_codec.cpp pins every position with a static_assert.
using WorldEventPayload = std::variant<
    WorldTransactionCommittedEvent,
    WorldTransactionRejectedEvent,
    EntityCreatedChange,
    ComponentAttachedChange,
    ComponentFieldChange,
    RelationAddedChange,
    RelationRemovedChange,
    MechanismSpawnedChange,
    MechanismFieldChange,
    MechanismLifecycleChange,
    MechanismAlgorithmInitializedChange,
    MechanismAlgorithmFaultChange,
    MechanismDestroyedChange,
    ScheduledEventAddedChange,
    ScheduledEventCancelledChange,
    RngStreamCreatedChange,
    RngStreamAdvancedChange,
    MechanismAlgorithmStateChange
>;

struct WorldEvent
{
    std::uint64_t sequence = 0;
    std::uint64_t tick = 0;
    std::uint64_t transactionSequence = 0;
    WorldEventPayload payload;
};

class WorldEventQueue
{
public:
    void PublishTransactionResult(
        std::uint64_t tick,
        std::uint64_t transactionSequence,
        const WorldTransactionResult& result
    );
    std::vector<WorldEvent> Drain();
    void Clear();
    bool Empty() const noexcept;
    std::size_t Size() const noexcept;
    const std::vector<WorldEvent>& Pending() const noexcept;

private:
    friend class persistence::RuntimePersistenceService;

    void Publish(
        std::uint64_t tick,
        std::uint64_t transactionSequence,
        WorldEventPayload payload
    );

    std::vector<WorldEvent> pending_;
    std::uint64_t nextSequence_ = 1;
};

}
