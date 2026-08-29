#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

#include "frozen_runtime_catalog.hpp"
#include "world_command_queue.hpp"
#include "world_event.hpp"

namespace dillen::runtime {

enum class MechanismSchedulerStatus
{
    Completed,
    TickSequenceInvalid,
    RuntimeCatalogNotFrozen
};

struct ScheduledWorldTransactionResult
{
    std::uint64_t sequence = 0;
    std::int32_t priority = 0;
    kernel::WorldTransaction transaction;
    kernel::WorldTransactionResult result;
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
    using TransactionExecutor = std::function<kernel::WorldTransactionResult(
        const kernel::WorldTransaction&,
        std::uint64_t
    )>;

    MechanismSchedulerTickResult RunTick(
        kernel::WorldCommandQueue& commands,
        kernel::WorldEventQueue& events,
        const kernel::FrozenRuntimeCatalog& catalog,
        std::uint64_t nextTick,
        std::uint64_t& currentTick,
        std::uint64_t& revision,
        const TransactionExecutor& executor
    );
};

}
