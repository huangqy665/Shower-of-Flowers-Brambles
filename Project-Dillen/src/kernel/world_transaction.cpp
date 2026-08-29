#include "world_transaction.hpp"

#include <utility>

namespace dillen::kernel {

WorldCommand WorldCommand::CreateEntity(EntityDefinitionId definition)
{
    return {EntityCreateCommand{definition}};
}

WorldCommand WorldCommand::SetComponentField(
    EntityId owner,
    ComponentTypeId component,
    ComponentFieldSlotId field,
    MechanismValue value
)
{
    return {ComponentSetFieldCommand{
        owner,
        component,
        field,
        std::move(value)
    }};
}

WorldCommand WorldCommand::AddRelation(
    RelationTypeId type,
    EntityId source,
    EntityId target
)
{
    return {RelationAddCommand{type, source, target}};
}

WorldCommand WorldCommand::RemoveRelation(RelationId relation)
{
    return {RelationRemoveCommand{relation}};
}

WorldCommand WorldCommand::SpawnMechanism(
    MechanismSpawnDefinitionId spawn
)
{
    return {MechanismSpawnCommand{spawn}};
}

WorldCommand WorldCommand::Mechanism(MechanismCommand command)
{
    return {std::move(command)};
}

WorldCommand WorldCommand::ScheduleEvent(
    AlgorithmEventTypeId type,
    MechanismInstanceId target,
    std::uint64_t dueTick,
    std::int32_t priority,
    MechanismValue payload
)
{
    return {ScheduledEventScheduleCommand{
        type,
        target,
        dueTick,
        priority,
        std::move(payload)
    }};
}

WorldCommand WorldCommand::CancelEvent(std::uint64_t sequence)
{
    return {ScheduledEventCancelCommand{sequence}};
}

WorldCommand WorldCommand::CreateRngStream(
    RngStreamId stream,
    std::uint64_t seed
)
{
    return {RngStreamCreateCommand{stream, seed}};
}

WorldCommand WorldCommand::AdvanceRngStream(
    RngStreamId stream,
    std::uint64_t expectedDrawCount,
    std::uint64_t count
)
{
    return {RngStreamAdvanceCommand{
        stream,
        expectedDrawCount,
        count
    }};
}

WorldTransaction WorldTransaction::FromMechanismCommands(
    std::vector<MechanismCommand> commands
)
{
    WorldTransaction transaction;
    transaction.commands.reserve(commands.size());
    for (MechanismCommand& command : commands)
    {
        transaction.commands.push_back(
            WorldCommand::Mechanism(std::move(command))
        );
    }
    return transaction;
}

WorldTransactionResult::operator bool() const noexcept
{
    return status == WorldTransactionStatus::Committed;
}

bool WorldTransactionResult::Changed() const noexcept
{
    return !changes.empty();
}

}
