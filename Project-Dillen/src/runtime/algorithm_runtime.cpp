#include "algorithm_runtime.hpp"

#include <algorithm>
#include <exception>
#include <utility>

#include "declarative_algorithm_vm.hpp"
#include "controlled_script_vm.hpp"

namespace dillen::runtime {

namespace {

kernel::AlgorithmEntryPoint EntryPointForStage(AlgorithmRuntimeStage stage)
{
    switch (stage)
    {
    case AlgorithmRuntimeStage::Create:
        return kernel::AlgorithmEntryPoint::Create;
    case AlgorithmRuntimeStage::Tick:
        return kernel::AlgorithmEntryPoint::Tick;
    case AlgorithmRuntimeStage::Event:
        return kernel::AlgorithmEntryPoint::Event;
    case AlgorithmRuntimeStage::Command:
        return kernel::AlgorithmEntryPoint::Command;
    case AlgorithmRuntimeStage::Destroy:
        return kernel::AlgorithmEntryPoint::Destroy;
    }
    return kernel::AlgorithmEntryPoint::None;
}

bool IsActiveAlgorithmInstance(const kernel::MechanismInstance& instance)
{
    return instance.algorithmInitialized
        && instance.lifecycle == kernel::MechanismLifecycleState::Active
        && !instance.algorithmFault.isolated
        && instance.algorithm;
}

bool HasContinuation(
    const kernel::MechanismInstance& instance,
    kernel::AlgorithmEntryPoint entryPoint
)
{
    return std::any_of(
        instance.algorithmContinuations.begin(),
        instance.algorithmContinuations.end(),
        [entryPoint](const kernel::ControlledScriptContinuation& value)
        {
            return value.entryPoint == entryPoint;
        }
    );
}

void SetFailure(
    AlgorithmInvocationResult& result,
    AlgorithmInvocationStatus status,
    kernel::AlgorithmFaultCode faultCode,
    std::string message
)
{
    result.status = status;
    result.faultCode = faultCode;
    result.message = std::move(message);
}

void SetBudgetFailure(
    AlgorithmInvocationResult& result,
    const AlgorithmExecutionBudget& budget
)
{
    result.budget = budget.Report();
    SetFailure(
        result,
        AlgorithmInvocationStatus::InstructionBudgetExceeded,
        kernel::AlgorithmFaultCode::InstructionBudgetExceeded,
        "Algorithm instruction budget was exceeded"
    );
}

}

const kernel::RuntimeCapabilityContract*
AlgorithmInvocationContext::FindCapability(
    kernel::CapabilityId capability
) const
{
    for (kernel::CapabilityBindingSlotId slot : capabilities)
    {
        const kernel::RuntimeCapabilityContract* contract =
            catalog.FindCapability(slot);
        if (contract != nullptr && contract->id == capability)
        {
            return contract;
        }
    }
    return nullptr;
}

AlgorithmExecutorRegisterResult AlgorithmExecutorRegistry::Register(
    AlgorithmExecutorBinding binding
)
{
    if (frozen_)
    {
        return AlgorithmExecutorRegisterResult::Frozen;
    }
    if (!binding.algorithm || binding.version == 0 || !binding.execute)
    {
        return AlgorithmExecutorRegisterResult::InvalidBinding;
    }
    const auto key = std::make_pair(
        binding.algorithm.value,
        binding.version
    );
    if (bindings_.find(key) != bindings_.end())
    {
        return AlgorithmExecutorRegisterResult::DuplicateBinding;
    }
    bindings_.emplace(key, std::move(binding));
    return AlgorithmExecutorRegisterResult::Added;
}

void AlgorithmExecutorRegistry::Freeze()
{
    frozen_ = true;
}

bool AlgorithmExecutorRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

std::size_t AlgorithmExecutorRegistry::Size() const noexcept
{
    return bindings_.size();
}

const AlgorithmExecutorBinding* AlgorithmExecutorRegistry::Find(
    kernel::AlgorithmId algorithm,
    std::uint32_t version
) const
{
    const auto iterator = bindings_.find({algorithm.value, version});
    return iterator == bindings_.end() ? nullptr : &iterator->second;
}

AlgorithmInvocationResult::operator bool() const noexcept
{
    return status == AlgorithmInvocationStatus::Completed
        || status == AlgorithmInvocationStatus::Preempted;
}

bool AlgorithmStageReport::Success() const noexcept
{
    return std::all_of(
        invocations.begin(),
        invocations.end(),
        [](const AlgorithmInvocationResult& invocation)
        {
            return static_cast<bool>(invocation);
        }
    );
}

std::size_t AlgorithmStageReport::CompletedCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        invocations.begin(),
        invocations.end(),
        [](const AlgorithmInvocationResult& invocation)
        {
            return static_cast<bool>(invocation);
        }
    ));
}

std::size_t AlgorithmStageReport::FailedCount() const noexcept
{
    return invocations.size() - CompletedCount();
}

AlgorithmRuntime::AlgorithmRuntime(
    const kernel::FrozenRuntimeCatalog& catalog,
    const AlgorithmExecutorRegistry& executors
)
    : catalog_(catalog),
      executors_(executors)
{
}

AlgorithmStageReport AlgorithmRuntime::DispatchCreate(
    const WorldQuerySnapshot& query,
    const kernel::DeterministicRngSnapshot& rng,
    std::uint64_t tick
) const
{
    AlgorithmStageReport report;
    const kernel::MechanismQuerySnapshot& snapshot = query.Mechanisms();
    for (const auto& entry : snapshot.All())
    {
        const kernel::MechanismInstance& instance = entry.second;
        if (instance.algorithm
            && !instance.algorithmInitialized
            && !instance.algorithmFault.isolated
            && instance.lifecycle == kernel::MechanismLifecycleState::Created)
        {
            report.invocations.push_back(Invoke(
                AlgorithmRuntimeStage::Create,
                tick,
                instance,
                query,
                rng,
                nullptr,
                nullptr,
                nullptr
            ));
        }
    }
    return report;
}

AlgorithmStageReport AlgorithmRuntime::DispatchTick(
    const WorldQuerySnapshot& query,
    const kernel::DeterministicRngSnapshot& rng,
    std::uint64_t tick
) const
{
    AlgorithmStageReport report;
    const kernel::MechanismQuerySnapshot& snapshot = query.Mechanisms();
    for (const auto& entry : snapshot.All())
    {
        const kernel::MechanismInstance& instance = entry.second;
        if (IsActiveAlgorithmInstance(instance))
        {
            const kernel::AlgorithmDescriptor* descriptor =
                catalog_.FindAlgorithm(
                    instance.algorithm,
                    instance.algorithmVersion
                );
            if (descriptor != nullptr
                && kernel::HasAlgorithmEntryPoint(
                    descriptor->entryPoints,
                    kernel::AlgorithmEntryPoint::Tick))
            {
                report.invocations.push_back(Invoke(
                    AlgorithmRuntimeStage::Tick,
                    tick,
                    instance,
                    query,
                    rng,
                    nullptr,
                    nullptr,
                    nullptr
                ));
            }
        }
    }
    return report;
}

AlgorithmStageReport AlgorithmRuntime::DispatchEvent(
    const WorldQuerySnapshot& query,
    const kernel::DeterministicRngSnapshot& rng,
    std::uint64_t tick,
    const std::vector<kernel::WorldEvent>& events,
    const std::vector<kernel::ScheduledAlgorithmEvent>& scheduledEvents
) const
{
    AlgorithmStageReport report;
    const kernel::MechanismQuerySnapshot& snapshot = query.Mechanisms();
    const auto dispatch = [this, &report, &query, &rng, tick](
        const kernel::MechanismInstance& instance,
        const kernel::WorldEvent* event,
        const kernel::ScheduledAlgorithmEvent* scheduledEvent)
    {
        if (!IsActiveAlgorithmInstance(instance)
            || HasContinuation(
                instance,
                kernel::AlgorithmEntryPoint::Event))
        {
            return;
        }
        const kernel::AlgorithmDescriptor* descriptor =
            catalog_.FindAlgorithm(
                instance.algorithm,
                instance.algorithmVersion
            );
        if (descriptor != nullptr
            && kernel::HasAlgorithmEntryPoint(
                descriptor->entryPoints,
                kernel::AlgorithmEntryPoint::Event))
        {
            report.invocations.push_back(Invoke(
                AlgorithmRuntimeStage::Event,
                tick,
                instance,
                query,
                rng,
                event,
                scheduledEvent,
                nullptr
            ));
        }
    };
    for (const kernel::ScheduledAlgorithmEvent& scheduled
        : scheduledEvents)
    {
        if (scheduled.target)
        {
            const kernel::MechanismInstance* target = snapshot.Find(
                scheduled.target
            );
            if (target != nullptr)
            {
                dispatch(*target, nullptr, &scheduled);
            }
            continue;
        }
        for (const auto& entry : snapshot.All())
        {
            dispatch(entry.second, nullptr, &scheduled);
        }
    }
    for (const kernel::WorldEvent& event : events)
    {
        for (const auto& entry : snapshot.All())
        {
            dispatch(entry.second, &event, nullptr);
        }
    }
    return report;
}

AlgorithmStageReport AlgorithmRuntime::DispatchCommand(
    const WorldQuerySnapshot& query,
    const kernel::DeterministicRngSnapshot& rng,
    std::uint64_t tick,
    const kernel::WorldTransaction& command
) const
{
    AlgorithmStageReport report;
    const kernel::MechanismQuerySnapshot& snapshot = query.Mechanisms();
    for (const auto& entry : snapshot.All())
    {
        const kernel::MechanismInstance& instance = entry.second;
        if (!IsActiveAlgorithmInstance(instance)
            || HasContinuation(
                instance,
                kernel::AlgorithmEntryPoint::Command))
        {
            continue;
        }
        const kernel::AlgorithmDescriptor* descriptor =
            catalog_.FindAlgorithm(
                instance.algorithm,
                instance.algorithmVersion
            );
        if (descriptor != nullptr
            && kernel::HasAlgorithmEntryPoint(
                descriptor->entryPoints,
                kernel::AlgorithmEntryPoint::Command))
        {
            report.invocations.push_back(Invoke(
                AlgorithmRuntimeStage::Command,
                tick,
                instance,
                query,
                rng,
                nullptr,
                nullptr,
                &command
            ));
        }
    }
    return report;
}

AlgorithmStageReport AlgorithmRuntime::DispatchDeferred(
    const WorldQuerySnapshot& query,
    const kernel::DeterministicRngSnapshot& rng,
    std::uint64_t tick
) const
{
    AlgorithmStageReport report;
    for (const auto& entry : query.Mechanisms().All())
    {
        const kernel::MechanismInstance& instance = entry.second;
        if (!IsActiveAlgorithmInstance(instance)) continue;
        for (const kernel::ControlledScriptContinuation& continuation
            : instance.algorithmContinuations)
        {
            AlgorithmRuntimeStage stage;
            if (continuation.entryPoint == kernel::AlgorithmEntryPoint::Event)
            {
                stage = AlgorithmRuntimeStage::Event;
            }
            else if (continuation.entryPoint
                == kernel::AlgorithmEntryPoint::Command)
            {
                stage = AlgorithmRuntimeStage::Command;
            }
            else
            {
                continue;
            }
            report.invocations.push_back(Invoke(
                stage,
                tick,
                instance,
                query,
                rng,
                nullptr,
                nullptr,
                nullptr
            ));
        }
    }
    return report;
}

AlgorithmStageReport AlgorithmRuntime::DispatchDestroy(
    const WorldQuerySnapshot& query,
    const kernel::DeterministicRngSnapshot& rng,
    std::uint64_t tick
) const
{
    AlgorithmStageReport report;
    for (const auto& entry : query.Mechanisms().All())
    {
        const kernel::MechanismInstance& instance = entry.second;
        if (!instance.algorithm
            || !instance.algorithmInitialized
            || instance.algorithmFault.isolated
            || !kernel::IsTerminalMechanismLifecycleState(
                instance.lifecycle))
        {
            continue;
        }
        const kernel::AlgorithmDescriptor* descriptor =
            catalog_.FindAlgorithm(
                instance.algorithm,
                instance.algorithmVersion
            );
        if (descriptor != nullptr
            && kernel::HasAlgorithmEntryPoint(
                descriptor->entryPoints,
                kernel::AlgorithmEntryPoint::Destroy))
        {
            report.invocations.push_back(Invoke(
                AlgorithmRuntimeStage::Destroy,
                tick,
                instance,
                query,
                rng,
                nullptr,
                nullptr,
                nullptr
            ));
        }
    }
    return report;
}

AlgorithmInvocationResult AlgorithmRuntime::Invoke(
    AlgorithmRuntimeStage stage,
    std::uint64_t tick,
    const kernel::MechanismInstance& instance,
    const WorldQuerySnapshot& query,
    const kernel::DeterministicRngSnapshot& rng,
    const kernel::WorldEvent* event,
    const kernel::ScheduledAlgorithmEvent* scheduledEvent,
    const kernel::WorldTransaction* command
) const
{
    AlgorithmInvocationResult result;
    result.stage = stage;
    result.target = instance.id;
    result.algorithm = instance.algorithm;
    result.algorithmVersion = instance.algorithmVersion;
    if (!catalog_.IsFrozen())
    {
        SetFailure(
            result,
            AlgorithmInvocationStatus::RuntimeCatalogNotFrozen,
            kernel::AlgorithmFaultCode::ContractUnavailable,
            "Frozen Runtime Catalog is required"
        );
        return result;
    }
    const kernel::AlgorithmDescriptor* descriptor = catalog_.FindAlgorithm(
        instance.algorithm,
        instance.algorithmVersion
    );
    if (descriptor == nullptr
        || !kernel::HasAlgorithmEntryPoint(
            descriptor->entryPoints,
            EntryPointForStage(stage)))
    {
        SetFailure(
            result,
            AlgorithmInvocationStatus::AlgorithmMissing,
            kernel::AlgorithmFaultCode::ContractUnavailable,
            "Algorithm descriptor or stage entry point is missing"
        );
        return result;
    }
    result.failurePolicy = descriptor->executionPolicy.failurePolicy;
    AlgorithmExecutionBudget budget(descriptor->executionPolicy);
    const auto& capabilities = catalog_.AlgorithmCapabilities(
        instance.algorithm,
        instance.algorithmVersion
    );
    for (kernel::CapabilityBindingSlotId slot : capabilities)
    {
        if (catalog_.FindCapability(slot) == nullptr)
        {
            SetFailure(
                result,
                AlgorithmInvocationStatus::CapabilityBindingMissing,
                kernel::AlgorithmFaultCode::ContractUnavailable,
                "Compiled Capability binding is invalid"
            );
            return result;
        }
    }
    const AlgorithmInvocationContext context{
        stage,
        tick,
        instance,
        query,
        query.Mechanisms(),
        rng,
        catalog_,
        capabilities,
        event,
        scheduledEvent,
        command,
        budget
    };
    if (descriptor->backend == kernel::AlgorithmBackend::Declarative)
    {
        const kernel::CompiledAlgorithmProgram* program =
            catalog_.FindAlgorithmProgram(instance.definition);
        if (program == nullptr)
        {
            SetFailure(
                result,
                AlgorithmInvocationStatus::DeclarativeProgramMissing,
                kernel::AlgorithmFaultCode::ContractUnavailable,
                "Compiled declarative program is missing"
            );
            return result;
        }
        try
        {
            DeclarativeAlgorithmResult execution =
                DeclarativeAlgorithmVm{}.Execute(
                    *program,
                    EntryPointForStage(stage),
                    context
                );
            result.budget = budget.Report();
            if (!execution)
            {
                if (execution.status
                    == DeclarativeAlgorithmStatus::
                        InstructionBudgetExceeded)
                {
                    SetBudgetFailure(result, budget);
                }
                else
                {
                    SetFailure(
                        result,
                        AlgorithmInvocationStatus::
                            DeclarativeExecutionFailed,
                        kernel::AlgorithmFaultCode::ExecutionRejected,
                        std::move(execution.message)
                    );
                }
                return result;
            }
            result.transaction = std::move(execution.transaction);
            result.status = AlgorithmInvocationStatus::Completed;
            return result;
        }
        catch (const std::exception& error)
        {
            result.budget = budget.Report();
            SetFailure(
                result,
                AlgorithmInvocationStatus::ExecutorException,
                kernel::AlgorithmFaultCode::ExecutorException,
                error.what()
            );
        }
        catch (...)
        {
            result.budget = budget.Report();
            SetFailure(
                result,
                AlgorithmInvocationStatus::ExecutorException,
                kernel::AlgorithmFaultCode::ExecutorException,
                "Declarative Algorithm raised an unknown exception"
            );
        }
        return result;
    }
    if (descriptor->backend == kernel::AlgorithmBackend::Script)
    {
        const kernel::CompiledControlledScriptProgram* program =
            catalog_.FindControlledScriptProgram(instance.definition);
        if (program == nullptr)
        {
            SetFailure(
                result,
                AlgorithmInvocationStatus::DeclarativeProgramMissing,
                kernel::AlgorithmFaultCode::ContractUnavailable,
                "Compiled Controlled Script program is missing"
            );
            return result;
        }
        try
        {
            ControlledScriptResult execution = ControlledScriptVm{}.Execute(
                *program,
                EntryPointForStage(stage),
                context,
                descriptor->executionPolicy
            );
            result.budget = budget.Report();
            if (!execution)
            {
                if (execution.status
                    == ControlledScriptStatus::InstructionBudgetExceeded)
                {
                    SetBudgetFailure(result, budget);
                }
                else if (execution.status
                    == ControlledScriptStatus::MemoryQuotaExceeded)
                {
                    SetFailure(
                        result,
                        AlgorithmInvocationStatus::ScriptMemoryQuotaExceeded,
                        kernel::AlgorithmFaultCode::
                            ScriptMemoryQuotaExceeded,
                        std::move(execution.message)
                    );
                }
                else
                {
                    SetFailure(
                        result,
                        AlgorithmInvocationStatus::ScriptExecutionFailed,
                        kernel::AlgorithmFaultCode::ExecutionRejected,
                        std::move(execution.message)
                    );
                }
                return result;
            }
            result.transaction = std::move(execution.transaction);
            result.status = execution.status
                    == ControlledScriptStatus::Preempted
                ? AlgorithmInvocationStatus::Preempted
                : AlgorithmInvocationStatus::Completed;
            return result;
        }
        catch (const std::exception& error)
        {
            result.budget = budget.Report();
            SetFailure(
                result,
                AlgorithmInvocationStatus::ExecutorException,
                kernel::AlgorithmFaultCode::ExecutorException,
                error.what()
            );
        }
        catch (...)
        {
            result.budget = budget.Report();
            SetFailure(
                result,
                AlgorithmInvocationStatus::ExecutorException,
                kernel::AlgorithmFaultCode::ExecutorException,
                "Controlled Script raised an unknown exception"
            );
        }
        return result;
    }
    if (!executors_.IsFrozen())
    {
        SetFailure(
            result,
            AlgorithmInvocationStatus::ExecutorRegistryNotFrozen,
            kernel::AlgorithmFaultCode::ContractUnavailable,
            "Algorithm Executor Registry is not frozen"
        );
        return result;
    }
    const AlgorithmExecutorBinding* executor = executors_.Find(
        instance.algorithm,
        instance.algorithmVersion
    );
    if (executor == nullptr)
    {
        SetFailure(
            result,
            AlgorithmInvocationStatus::ExecutorMissing,
            kernel::AlgorithmFaultCode::BackendUnavailable,
            "Algorithm Executor binding is missing"
        );
        return result;
    }
    if (executor->backend != descriptor->backend)
    {
        SetFailure(
            result,
            AlgorithmInvocationStatus::BackendMismatch,
            kernel::AlgorithmFaultCode::ContractUnavailable,
            "Algorithm Executor backend does not match descriptor"
        );
        return result;
    }
    try
    {
        AlgorithmExecutionOutput output;
        const bool accepted = executor->execute(context, output);
        if (!budget.Checkpoint())
        {
            SetBudgetFailure(result, budget);
            return result;
        }
        result.budget = budget.Report();
        if (!accepted)
        {
            SetFailure(
                result,
                AlgorithmInvocationStatus::ExecutorRejected,
                kernel::AlgorithmFaultCode::ExecutionRejected,
                "Algorithm Executor rejected invocation"
            );
            return result;
        }
        result.transaction = std::move(output.transaction);
        result.status = AlgorithmInvocationStatus::Completed;
        return result;
    }
    catch (const std::exception& error)
    {
        result.budget = budget.Report();
        SetFailure(
            result,
            AlgorithmInvocationStatus::ExecutorException,
            kernel::AlgorithmFaultCode::ExecutorException,
            error.what()
        );
    }
    catch (...)
    {
        result.budget = budget.Report();
        SetFailure(
            result,
            AlgorithmInvocationStatus::ExecutorException,
            kernel::AlgorithmFaultCode::ExecutorException,
            "Algorithm Executor raised an unknown exception"
        );
    }
    return result;
}

}
