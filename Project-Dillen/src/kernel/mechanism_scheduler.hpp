#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "mechanism_definition_registry.hpp"
#include "mechanism_instance_store.hpp"
#include "mechanism_query_snapshot.hpp"
#include "mechanism_schema_registry.hpp"
#include "world_command_queue.hpp"
#include "world_event.hpp"

namespace dillen::kernel {

enum class MechanismSchedulerStatus
{
    Completed,
    TickSequenceInvalid,
    DefinitionRegistryNotFrozen,
    SchemaRegistryNotFrozen
};

struct ScheduledWorldTransactionResult
{
    std::uint64_t sequence = 0;
    WorldTransactionResult result;
};

struct MechanismSchedulerTickResult
{
    MechanismSchedulerStatus status = MechanismSchedulerStatus::Completed;
    std::uint64_t tick = 0;
    std::uint64_t revision = 0;
    std::size_t processedTransactions = 0;
    std::size_t committedTransactions = 0;
    std::size_t rejectedTransactions = 0;
    std::vector<ScheduledWorldTransactionResult> transactions;

    explicit operator bool() const noexcept;
};

class MechanismScheduler
{
public:
    MechanismSchedulerTickResult RunTick(
        MechanismInstanceStore& mechanisms,
        WorldCommandQueue& commands,
        WorldEventQueue& events,
        MechanismQuerySnapshot& snapshot,
        const MechanismDefinitionRegistry& definitions,
        const MechanismSchemaRegistry& schemas,
        std::uint64_t nextTick,
        std::uint64_t& revision
    );
    void Reset(std::uint64_t tick = 0) noexcept;
    std::uint64_t CurrentTick() const noexcept;

private:
    std::uint64_t currentTick_ = 0;
};

}
