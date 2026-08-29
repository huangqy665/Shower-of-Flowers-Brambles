#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "algorithm_execution_budget.hpp"
#include "algorithm_inbox.hpp"
#include "deterministic_rng.hpp"
#include "frozen_runtime_catalog.hpp"
#include "world_query_snapshot.hpp"
#include "world_event.hpp"
#include "world_transaction.hpp"

namespace dillen::runtime {

enum class AlgorithmRuntimeStage
{
    Create,
    Tick,
    Event,
    Command,
    Destroy
};

struct AlgorithmInvocationContext
{
    AlgorithmRuntimeStage stage = AlgorithmRuntimeStage::Tick;
    std::uint64_t tick = 0;
    const kernel::MechanismInstance& instance;
    const WorldQuerySnapshot& query;
    const kernel::MechanismQuerySnapshot& snapshot;
    const kernel::DeterministicRngSnapshot& rng;
    const kernel::FrozenRuntimeCatalog& catalog;
    const std::vector<kernel::CapabilityBindingSlotId>& capabilities;
    const kernel::WorldEvent* event = nullptr;
    const kernel::ScheduledAlgorithmEvent* scheduledEvent = nullptr;
    const kernel::WorldTransaction* command = nullptr;
    AlgorithmExecutionBudget& budget;

    const kernel::RuntimeCapabilityContract* FindCapability(
        kernel::CapabilityId capability
    ) const;
};

struct AlgorithmExecutionOutput
{
    kernel::WorldTransaction transaction;
};

using AlgorithmExecutor = std::function<bool(
    const AlgorithmInvocationContext&,
    AlgorithmExecutionOutput&
)>;

struct AlgorithmExecutorBinding
{
    kernel::AlgorithmId algorithm;
    std::uint32_t version = 0;
    kernel::AlgorithmBackend backend = kernel::AlgorithmBackend::Declarative;
    AlgorithmExecutor execute;
};

enum class AlgorithmExecutorRegisterResult
{
    Added,
    InvalidBinding,
    DuplicateBinding,
    Frozen
};

class AlgorithmExecutorRegistry
{
public:
    AlgorithmExecutorRegisterResult Register(
        AlgorithmExecutorBinding binding
    );
    void Freeze();
    bool IsFrozen() const noexcept;
    std::size_t Size() const noexcept;
    const AlgorithmExecutorBinding* Find(
        kernel::AlgorithmId algorithm,
        std::uint32_t version
    ) const;

private:
    std::map<
        std::pair<std::uint64_t, std::uint32_t>,
        AlgorithmExecutorBinding
    > bindings_;
    bool frozen_ = false;
};

enum class AlgorithmInvocationStatus
{
    Completed,
    RuntimeCatalogNotFrozen,
    ExecutorRegistryNotFrozen,
    AlgorithmMissing,
    InstanceUnavailable,
    InstanceIsolated,
    DeclarativeProgramMissing,
    DeclarativeExecutionFailed,
    ScriptBackendUnavailable,
    ExecutorMissing,
    BackendMismatch,
    CapabilityBindingMissing,
    InstructionBudgetExceeded,
    ExecutorRejected,
    ExecutorException,
    TransactionRejected
};

struct AlgorithmInvocationResult
{
    AlgorithmRuntimeStage stage = AlgorithmRuntimeStage::Tick;
    kernel::MechanismInstanceId target;
    kernel::AlgorithmId algorithm;
    std::uint32_t algorithmVersion = 0;
    AlgorithmInvocationStatus status = AlgorithmInvocationStatus::Completed;
    kernel::AlgorithmFaultCode faultCode = kernel::AlgorithmFaultCode::None;
    kernel::AlgorithmFailurePolicy failurePolicy =
        kernel::AlgorithmFailurePolicy::FailInstance;
    AlgorithmBudgetReport budget;
    std::string message;
    kernel::WorldTransaction transaction;

    explicit operator bool() const noexcept;
};

struct AlgorithmStageReport
{
    std::vector<AlgorithmInvocationResult> invocations;

    bool Success() const noexcept;
    std::size_t CompletedCount() const noexcept;
    std::size_t FailedCount() const noexcept;
};

class AlgorithmRuntime
{
public:
    AlgorithmRuntime(
        const kernel::FrozenRuntimeCatalog& catalog,
        const AlgorithmExecutorRegistry& executors
    );

    AlgorithmStageReport DispatchCreate(
        const WorldQuerySnapshot& query,
        const kernel::DeterministicRngSnapshot& rng,
        std::uint64_t tick
    ) const;
    AlgorithmStageReport DispatchTick(
        const WorldQuerySnapshot& query,
        const kernel::DeterministicRngSnapshot& rng,
        std::uint64_t tick
    ) const;
    AlgorithmStageReport DispatchEvent(
        const WorldQuerySnapshot& query,
        const kernel::DeterministicRngSnapshot& rng,
        std::uint64_t tick,
        const std::vector<kernel::WorldEvent>& events,
        const std::vector<kernel::ScheduledAlgorithmEvent>& scheduledEvents
    ) const;
    AlgorithmStageReport DispatchCommand(
        const WorldQuerySnapshot& query,
        const kernel::DeterministicRngSnapshot& rng,
        std::uint64_t tick,
        const kernel::WorldTransaction& command
    ) const;
    AlgorithmStageReport DispatchDestroy(
        const WorldQuerySnapshot& query,
        const kernel::DeterministicRngSnapshot& rng,
        std::uint64_t tick
    ) const;

private:
    AlgorithmInvocationResult Invoke(
        AlgorithmRuntimeStage stage,
        std::uint64_t tick,
        const kernel::MechanismInstance& instance,
        const WorldQuerySnapshot& query,
        const kernel::DeterministicRngSnapshot& rng,
        const kernel::WorldEvent* event,
        const kernel::ScheduledAlgorithmEvent* scheduledEvent,
        const kernel::WorldTransaction* command
    ) const;

    const kernel::FrozenRuntimeCatalog& catalog_;
    const AlgorithmExecutorRegistry& executors_;
};

}
