#pragma once

#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

#include "algorithm_inbox.hpp"
#include "mechanism_change.hpp"
#include "mechanism_command.hpp"
#include "mechanism_transaction.hpp"

namespace dillen::kernel {

struct EntityCreateCommand
{
    EntityDefinitionId definition;
};

struct ComponentSetFieldCommand
{
    EntityId owner;
    ComponentTypeId component;
    ComponentFieldSlotId field;
    MechanismValue value;
};

struct RelationAddCommand
{
    RelationTypeId type;
    EntityId source;
    EntityId target;
};

struct RelationRemoveCommand
{
    RelationId relation;
};

struct MechanismSpawnCommand
{
    MechanismSpawnDefinitionId spawn;
};

struct ScheduledEventScheduleCommand
{
    AlgorithmEventTypeId type;
    MechanismInstanceId target;
    std::uint64_t dueTick = 0;
    std::int32_t priority = 0;
    MechanismValue payload;
};

struct ScheduledEventCancelCommand
{
    std::uint64_t sequence = 0;
};

struct RngStreamCreateCommand
{
    RngStreamId stream;
    std::uint64_t seed = 0;
};

struct RngStreamAdvanceCommand
{
    RngStreamId stream;
    std::uint64_t expectedDrawCount = 0;
    std::uint64_t count = 0;
};

using WorldCommandPayload = std::variant<
    EntityCreateCommand,
    ComponentSetFieldCommand,
    RelationAddCommand,
    RelationRemoveCommand,
    MechanismSpawnCommand,
    MechanismCommand,
    ScheduledEventScheduleCommand,
    ScheduledEventCancelCommand,
    RngStreamCreateCommand,
    RngStreamAdvanceCommand
>;

struct WorldCommand
{
    WorldCommandPayload payload;

    static WorldCommand CreateEntity(EntityDefinitionId definition);
    static WorldCommand SetComponentField(
        EntityId owner,
        ComponentTypeId component,
        ComponentFieldSlotId field,
        MechanismValue value
    );
    static WorldCommand AddRelation(
        RelationTypeId type,
        EntityId source,
        EntityId target
    );
    static WorldCommand RemoveRelation(RelationId relation);
    static WorldCommand SpawnMechanism(
        MechanismSpawnDefinitionId spawn
    );
    static WorldCommand Mechanism(MechanismCommand command);
    static WorldCommand ScheduleEvent(
        AlgorithmEventTypeId type,
        MechanismInstanceId target,
        std::uint64_t dueTick,
        std::int32_t priority,
        MechanismValue payload
    );
    static WorldCommand CancelEvent(std::uint64_t sequence);
    static WorldCommand CreateRngStream(
        RngStreamId stream,
        std::uint64_t seed
    );
    static WorldCommand AdvanceRngStream(
        RngStreamId stream,
        std::uint64_t expectedDrawCount,
        std::uint64_t count
    );
};

struct WorldTransaction
{
    std::vector<WorldCommand> commands;

    static WorldTransaction FromMechanismCommands(
        std::vector<MechanismCommand> commands
    );
};

struct EntityCreatedChange
{
    EntityId entity;
    EntityDefinitionId definition;
};

struct ComponentAttachedChange
{
    EntityId owner;
    ComponentTypeId component;
};

struct ComponentFieldChange
{
    EntityId owner;
    ComponentTypeId component;
    ComponentFieldSlotId field;
    MechanismValue previousValue;
    MechanismValue currentValue;
};

struct RelationAddedChange
{
    RelationId relation;
    RelationTypeId type;
    EntityId source;
    EntityId target;
};

struct RelationRemovedChange
{
    RelationId relation;
    RelationTypeId type;
    EntityId source;
    EntityId target;
};

struct MechanismSpawnedChange
{
    MechanismInstanceId instance;
    MechanismSpawnDefinitionId spawn;
};

struct ScheduledEventAddedChange
{
    ScheduledAlgorithmEvent event;
};

struct ScheduledEventCancelledChange
{
    ScheduledAlgorithmEvent event;
};

struct RngStreamCreatedChange
{
    RngStreamId stream;
    std::uint64_t seed = 0;
};

struct RngStreamAdvancedChange
{
    RngStreamId stream;
    std::uint64_t previousDrawCount = 0;
    std::uint64_t currentDrawCount = 0;
};

using WorldChange = std::variant<
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
    RngStreamAdvancedChange
>;

enum class WorldTransactionStatus
{
    Committed,
    RuntimeCatalogNotFrozen,
    TickRegression,
    EntityRejected,
    ComponentRejected,
    RelationRejected,
    MechanismRejected,
    ScheduledEventRejected,
    RngRejected
};

struct WorldTransactionResult
{
    WorldTransactionStatus status = WorldTransactionStatus::Committed;
    std::size_t commandIndex = 0;
    std::uint64_t subject = 0;
    MechanismTransactionResult mechanism;
    std::vector<WorldChange> changes;

    explicit operator bool() const noexcept;
    bool Changed() const noexcept;
};

}
