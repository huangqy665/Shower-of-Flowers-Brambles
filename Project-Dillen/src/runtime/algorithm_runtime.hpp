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
    ScriptExecutionFailed,
    ScriptMemoryQuotaExceeded,
    Preempted,
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

// Order in which a stage's planned invocations are actually executed.
//
// The threading contract (memo section 3.9) says algorithm dispatch may run on
// worker threads and that determinism is guaranteed by construction rather than
// by scheduling: each result is written to the slot fixed by its position in
// snapshot enumeration order, so which invocation finishes first cannot matter.
// Once a worker pool exists, execution order is whatever the scheduler picks.
//
// `Reversed` makes that claim testable before the pool exists. It changes only
// the order in which phase 2 fills the slots -- never the plan, never the slot
// assignment -- so a world run under it must produce byte-identical saves and
// replay checksums. thread_contract_probe asserts exactly that.
//
// It is not a substitute for the 1-vs-N probe that has to land with the worker
// pool: reversal proves order independence, not memory safety under concurrent
// execution. What it does catch is the failure mode that would make the pool
// unsafe in the first place -- an invocation that reads state another
// invocation in the same stage wrote, or a slot assignment that depends on
// execution order.
enum class DispatchExecutionOrder
{
    Enumeration,
    Reversed
};

class AlgorithmRuntime
{
public:
    AlgorithmRuntime(
        const kernel::FrozenRuntimeCatalog& catalog,
        const AlgorithmExecutorRegistry& executors
    );

    void SetExecutionOrder(DispatchExecutionOrder order) noexcept;

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
    AlgorithmStageReport DispatchDeferred(
        const WorldQuerySnapshot& query,
        const kernel::DeterministicRngSnapshot& rng,
        std::uint64_t tick
    ) const;

private:
    // Dispatch is two-phase, and the split is the threading contract made
    // structural rather than aspirational.
    //
    // Phase 1 walks the Query Snapshot in enumeration order and records what
    // to invoke. It is pure filtering over immutable state, it decides every
    // slot position, and it stays single-threaded.
    //
    // Phase 2 sizes the result vector to the plan and writes plan[i]'s result
    // into invocations[i]. Nothing in it depends on the order the entries are
    // visited, which is what lets a worker pool run it later -- and what
    // DispatchExecutionOrder::Reversed exercises today.
    //
    // The previous shape appended each result with push_back inside the
    // filtering loop, which coupled execution order to slot order and could
    // not have been parallelised at all.
    struct PlannedInvocation
    {
        AlgorithmRuntimeStage stage = AlgorithmRuntimeStage::Tick;
        const kernel::MechanismInstance* instance = nullptr;
        const kernel::WorldEvent* event = nullptr;
        const kernel::ScheduledAlgorithmEvent* scheduledEvent = nullptr;
        const kernel::WorldTransaction* command = nullptr;
    };

    AlgorithmStageReport ExecutePlan(
        const std::vector<PlannedInvocation>& plan,
        const WorldQuerySnapshot& query,
        const kernel::DeterministicRngSnapshot& rng,
        std::uint64_t tick
    ) const;

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
    DispatchExecutionOrder executionOrder_ =
        DispatchExecutionOrder::Enumeration;
};

}
