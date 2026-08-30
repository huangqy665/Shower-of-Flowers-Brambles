#include "kernel_runtime.hpp"

#include <iterator>
#include <optional>
#include <utility>

#include "world_transaction_executor.hpp"

namespace dillen::runtime {

namespace {

const AlgorithmExecutorRegistry& EmptyAlgorithmExecutors()
{
    static const AlgorithmExecutorRegistry registry = []
    {
        AlgorithmExecutorRegistry value;
        value.Freeze();
        return value;
    }();
    return registry;
}

kernel::AlgorithmFaultStage FaultStageFor(AlgorithmRuntimeStage stage)
{
    switch (stage)
    {
    case AlgorithmRuntimeStage::Create:
        return kernel::AlgorithmFaultStage::Create;
    case AlgorithmRuntimeStage::Tick:
        return kernel::AlgorithmFaultStage::Tick;
    case AlgorithmRuntimeStage::Event:
        return kernel::AlgorithmFaultStage::Event;
    case AlgorithmRuntimeStage::Command:
        return kernel::AlgorithmFaultStage::Command;
    case AlgorithmRuntimeStage::Destroy:
        return kernel::AlgorithmFaultStage::Destroy;
    }
    return kernel::AlgorithmFaultStage::Tick;
}

}

KernelRuntime::KernelRuntime(
    world::AuthoritativeWorld& world,
    const kernel::FrozenRuntimeCatalog& catalog
)
    : KernelRuntime(world, catalog, EmptyAlgorithmExecutors())
{
}

KernelRuntime::KernelRuntime(
    world::AuthoritativeWorld& world,
    const kernel::FrozenRuntimeCatalog& catalog,
    const AlgorithmExecutorRegistry& executors
)
    : world_(world),
      catalog_(catalog),
      algorithmRuntime_(catalog, executors)
{
    PublishSnapshots();
}

const kernel::WorldCommandQueue& KernelRuntime::Commands() const noexcept
{
    return commands_;
}

const kernel::WorldEventQueue& KernelRuntime::Events() const noexcept
{
    return events_;
}

const WorldQuerySnapshot& KernelRuntime::Query() const noexcept
{
    return *querySnapshot_;
}

WorldQuerySnapshotHandle KernelRuntime::AcquireQuerySnapshot()
    const noexcept
{
    return queryService_.Acquire();
}

const kernel::MechanismQuerySnapshot& KernelRuntime::Snapshot() const noexcept
{
    return querySnapshot_->Mechanisms();
}

const kernel::DeterministicRngSnapshot&
KernelRuntime::RngSnapshot() const noexcept
{
    return rngSnapshot_;
}

const AlgorithmStageReport& KernelRuntime::LastCreateAlgorithms()
    const noexcept
{
    return lastCreateAlgorithms_;
}

const AlgorithmStageReport& KernelRuntime::LastTickAlgorithms()
    const noexcept
{
    return lastTickAlgorithms_;
}

const AlgorithmStageReport& KernelRuntime::LastEventAlgorithms()
    const noexcept
{
    return lastEventAlgorithms_;
}

const AlgorithmStageReport& KernelRuntime::LastCommandAlgorithms()
    const noexcept
{
    return lastCommandAlgorithms_;
}

const AlgorithmStageReport& KernelRuntime::LastDestroyAlgorithms()
    const noexcept
{
    return lastDestroyAlgorithms_;
}

std::uint64_t KernelRuntime::Enqueue(
    kernel::WorldTransaction transaction,
    std::uint64_t notBeforeTick,
    std::int32_t priority
)
{
    return commands_.Enqueue(
        std::move(transaction),
        notBeforeTick,
        priority
    );
}

kernel::WorldTransactionResult KernelRuntime::ApplyImmediate(
    const kernel::WorldTransaction& transaction,
    std::uint64_t currentTick
)
{
    if (currentTick < world_.tick_)
    {
        return ApplyImmediateCore(transaction, currentTick);
    }
    lastCommandAlgorithms_ = algorithmRuntime_.DispatchCommand(
        *querySnapshot_,
        rngSnapshot_,
        currentTick,
        transaction
    );
    kernel::WorldTransactionResult result = ApplyImmediateCore(
        transaction,
        currentTick
    );
    if (result)
    {
        ApplyAlgorithmReport(
            lastCommandAlgorithms_,
            currentTick,
            AlgorithmCommitMode::Standard
        );
    }
    return result;
}

kernel::WorldTransactionResult KernelRuntime::ApplyImmediateCore(
    const kernel::WorldTransaction& transaction,
    std::uint64_t currentTick
)
{
    const std::uint64_t sequence = commands_.ReserveSequence();
    if (currentTick < world_.tick_)
    {
        kernel::WorldTransactionResult rejected;
        rejected.status = kernel::WorldTransactionStatus::TickRegression;
        rejected.mechanism.status =
            kernel::MechanismTransactionStatus::TickRegression;
        events_.PublishTransactionResult(currentTick, sequence, rejected);
        return rejected;
    }

    kernel::WorldTransactionResult result =
        world::WorldTransactionExecutor{}.Apply(
        world_,
        transaction,
        catalog_,
        currentTick
    );
    events_.PublishTransactionResult(currentTick, sequence, result);
    CaptureAlgorithmEvents();
    if (result)
    {
        if (result.Changed())
        {
            ++world_.revision_;
        }
        world_.tick_ = currentTick;
        PublishSnapshots();
    }
    return result;
}

kernel::MechanismTransactionResult
KernelRuntime::ApplyMechanismImmediate(
    const std::vector<kernel::MechanismCommand>& commands,
    std::uint64_t currentTick
)
{
    kernel::WorldTransactionResult result = ApplyImmediate(
        kernel::WorldTransaction::FromMechanismCommands(commands),
        currentTick
    );
    if (result.status
        == kernel::WorldTransactionStatus::RuntimeCatalogNotFrozen)
    {
        result.mechanism.status =
            kernel::MechanismTransactionStatus::RuntimeCatalogNotFrozen;
    }
    return std::move(result.mechanism);
}

MechanismSchedulerTickResult KernelRuntime::RunTick(
    std::uint64_t nextTick
)
{
    lastCreateAlgorithms_ = {};
    lastTickAlgorithms_ = {};
    lastEventAlgorithms_ = {};
    lastCommandAlgorithms_ = {};
    lastDestroyAlgorithms_ = {};
    MechanismSchedulerTickResult result = scheduler_.RunTick(
        commands_,
        events_,
        catalog_,
        nextTick,
        world_.tick_,
        world_.revision_,
        [this](
            const kernel::WorldTransaction& transaction,
            std::uint64_t tick)
        {
            return world::WorldTransactionExecutor{}.Apply(
                world_,
                transaction,
                catalog_,
                tick
            );
        }
    );
    if (!result)
    {
        return result;
    }
    PublishSnapshots();

    CaptureAlgorithmEvents();
    std::vector<kernel::WorldEvent> undispatchedEvents;
    undispatchedEvents.swap(pendingAlgorithmEvents_);

    lastCreateAlgorithms_ = algorithmRuntime_.DispatchCreate(
        *querySnapshot_,
        rngSnapshot_,
        nextTick
    );
    ApplyAlgorithmReport(
        lastCreateAlgorithms_,
        nextTick,
        AlgorithmCommitMode::CompleteCreate
    );

    AlgorithmStageReport deferredAlgorithms =
        algorithmRuntime_.DispatchDeferred(
            *querySnapshot_,
            rngSnapshot_,
            nextTick
        );
    ApplyAlgorithmReport(
        deferredAlgorithms,
        nextTick,
        AlgorithmCommitMode::Standard
    );
    for (AlgorithmInvocationResult& invocation
        : deferredAlgorithms.invocations)
    {
        AlgorithmStageReport& destination = invocation.stage
                == AlgorithmRuntimeStage::Event
            ? lastEventAlgorithms_
            : lastCommandAlgorithms_;
        destination.invocations.push_back(std::move(invocation));
    }

    std::vector<kernel::ScheduledAlgorithmEvent> scheduledEvents =
        world_.algorithmInbox_.TakeReady(nextTick);
    if (!scheduledEvents.empty())
    {
        ++world_.revision_;
        PublishSnapshots();
    }

    lastEventAlgorithms_ = algorithmRuntime_.DispatchEvent(
        *querySnapshot_,
        rngSnapshot_,
        nextTick,
        undispatchedEvents,
        scheduledEvents
    );
    ApplyAlgorithmReport(
        lastEventAlgorithms_,
        nextTick,
        AlgorithmCommitMode::Standard
    );

    for (const ScheduledWorldTransactionResult& scheduled
        : result.transactions)
    {
        if (!scheduled.result)
        {
            continue;
        }
        AlgorithmStageReport commandReport =
            algorithmRuntime_.DispatchCommand(
                *querySnapshot_,
                rngSnapshot_,
                nextTick,
                scheduled.transaction
            );
        ApplyAlgorithmReport(
            commandReport,
            nextTick,
            AlgorithmCommitMode::Standard
        );
        lastCommandAlgorithms_.invocations.insert(
            lastCommandAlgorithms_.invocations.end(),
            std::make_move_iterator(
                commandReport.invocations.begin()
            ),
            std::make_move_iterator(
                commandReport.invocations.end()
            )
        );
    }

    lastTickAlgorithms_ = algorithmRuntime_.DispatchTick(
        *querySnapshot_,
        rngSnapshot_,
        nextTick
    );
    ApplyAlgorithmReport(
        lastTickAlgorithms_,
        nextTick,
        AlgorithmCommitMode::Standard
    );
    lastDestroyAlgorithms_ = algorithmRuntime_.DispatchDestroy(
        *querySnapshot_,
        rngSnapshot_,
        nextTick
    );
    ApplyAlgorithmReport(
        lastDestroyAlgorithms_,
        nextTick,
        AlgorithmCommitMode::DestroyInstance
    );
    result.revision = world_.revision_;
    return result;
}

void KernelRuntime::PublishSnapshots()
{
    querySnapshot_ = queryService_.Publish(world_);
    rngSnapshot_.Publish(
        world_.rngStreams_,
        world_.tick_,
        world_.revision_
    );
}

void KernelRuntime::CaptureAlgorithmEvents()
{
    for (const kernel::WorldEvent& event : events_.Pending())
    {
        if (event.sequence > lastAlgorithmEventSequence_)
        {
            pendingAlgorithmEvents_.push_back(event);
            lastAlgorithmEventSequence_ = event.sequence;
        }
    }
}

std::vector<kernel::WorldEvent> KernelRuntime::DrainEvents()
{
    return events_.Drain();
}

bool KernelRuntime::TryApplyAlgorithmReportBatched(
    AlgorithmStageReport& report,
    std::uint64_t currentTick,
    AlgorithmCommitMode mode
)
{
    // Any failed invocation needs ApplyAlgorithmFault, which commits its own
    // transaction and reads the republished snapshot. Leave the whole stage to
    // the per-transaction path rather than model that here.
    for (const AlgorithmInvocationResult& invocation : report.invocations)
    {
        if (!invocation)
        {
            return false;
        }
    }

    // The batch is created lazily, on the first invocation that actually has
    // commands to apply. A stage whose invocations all produce empty
    // transactions must stay a complete no-op -- no store copy, no sequence,
    // no snapshot publication -- exactly as the per-transaction path is.
    std::optional<world::WorldTransactionBatch> batch;
    std::vector<kernel::WorldTransactionResult> committed;
    committed.reserve(report.invocations.size());
    for (const AlgorithmInvocationResult& invocation : report.invocations)
    {
        // Availability and isolation are read from the batch's working copies
        // once it exists, since those already reflect every transaction
        // applied earlier in this stage -- which is what the per-transaction
        // path gets from republishing the Query Snapshot after each commit.
        // Before the first Apply the published snapshot is still in sync with
        // the authoritative world, so it answers the same question.
        const kernel::MechanismInstance* current = batch
            ? batch->Mechanisms().Find(invocation.target)
            : querySnapshot_->Mechanisms().Find(invocation.target);
        if (current == nullptr || current->algorithmFault.isolated)
        {
            return false;
        }
        kernel::WorldTransaction transaction = invocation.transaction;
        if (mode == AlgorithmCommitMode::CompleteCreate
            && invocation.status == AlgorithmInvocationStatus::Completed)
        {
            transaction.commands.push_back(kernel::WorldCommand::Mechanism(
                kernel::MechanismCommand::CompleteAlgorithmCreate(
                    invocation.target
                )
            ));
        }
        else if (mode == AlgorithmCommitMode::DestroyInstance
            && invocation.status == AlgorithmInvocationStatus::Completed)
        {
            transaction.commands.push_back(kernel::WorldCommand::Mechanism(
                kernel::MechanismCommand::Destroy(invocation.target)
            ));
        }
        if (transaction.commands.empty())
        {
            continue;
        }
        if (!batch)
        {
            batch.emplace(world_, catalog_, currentTick);
            if (!batch->IsOpen())
            {
                return false;
            }
        }
        kernel::WorldTransactionResult result = batch->Apply(transaction);
        if (!result)
        {
            return false;
        }
        committed.push_back(std::move(result));
    }

    if (committed.empty())
    {
        return true;
    }
    batch->Commit();
    // Sequence reservation and result publication are deferred to here so a
    // discarded batch consumes no sequence numbers: the values handed out are
    // the same ones, in the same order, the per-transaction path would use.
    for (const kernel::WorldTransactionResult& result : committed)
    {
        const std::uint64_t sequence = commands_.ReserveSequence();
        events_.PublishTransactionResult(currentTick, sequence, result);
        if (result.Changed())
        {
            ++world_.revision_;
        }
    }
    CaptureAlgorithmEvents();
    world_.tick_ = currentTick;
    PublishSnapshots();
    return true;
}

void KernelRuntime::ApplyAlgorithmReport(
    AlgorithmStageReport& report,
    std::uint64_t currentTick,
    AlgorithmCommitMode mode
)
{
    if (TryApplyAlgorithmReportBatched(report, currentTick, mode))
    {
        return;
    }
    for (AlgorithmInvocationResult& invocation : report.invocations)
    {
        const kernel::MechanismInstance* current =
            querySnapshot_->Mechanisms().Find(invocation.target);
        if (current == nullptr)
        {
            invocation.status =
                AlgorithmInvocationStatus::InstanceUnavailable;
            invocation.faultCode = kernel::AlgorithmFaultCode::None;
            invocation.message =
                "Algorithm target is no longer available";
            continue;
        }
        if (current->algorithmFault.isolated)
        {
            invocation.status = AlgorithmInvocationStatus::InstanceIsolated;
            invocation.faultCode = kernel::AlgorithmFaultCode::None;
            invocation.message =
                "Algorithm target was isolated earlier in this stage";
            continue;
        }
        if (!invocation)
        {
            ApplyAlgorithmFault(invocation, currentTick);
            continue;
        }
        kernel::WorldTransaction transaction = invocation.transaction;
        if (mode == AlgorithmCommitMode::CompleteCreate
            && invocation.status == AlgorithmInvocationStatus::Completed)
        {
            transaction.commands.push_back(kernel::WorldCommand::Mechanism(
                kernel::MechanismCommand::CompleteAlgorithmCreate(
                    invocation.target
                )
            ));
        }
        else if (mode == AlgorithmCommitMode::DestroyInstance
            && invocation.status == AlgorithmInvocationStatus::Completed)
        {
            transaction.commands.push_back(kernel::WorldCommand::Mechanism(
                kernel::MechanismCommand::Destroy(invocation.target)
            ));
        }
        if (transaction.commands.empty())
        {
            continue;
        }
        const kernel::WorldTransactionResult committed = ApplyImmediateCore(
            transaction,
            currentTick
        );
        if (!committed)
        {
            invocation.status = AlgorithmInvocationStatus::TransactionRejected;
            invocation.faultCode =
                kernel::AlgorithmFaultCode::TransactionRejected;
            invocation.message = "Algorithm output transaction was rejected";
            ApplyAlgorithmFault(invocation, currentTick);
        }
    }
}

void KernelRuntime::ApplyAlgorithmFault(
    AlgorithmInvocationResult& invocation,
    std::uint64_t currentTick
)
{
    const kernel::MechanismInstance* instance =
        querySnapshot_->Mechanisms().Find(invocation.target);
    if (instance == nullptr
        || invocation.faultCode == kernel::AlgorithmFaultCode::None)
    {
        return;
    }

    std::vector<kernel::MechanismCommand> commands;
    commands.push_back(kernel::MechanismCommand::RecordAlgorithmFault(
        invocation.target,
        invocation.faultCode,
        FaultStageFor(invocation.stage)
    ));
    if (invocation.failurePolicy
        == kernel::AlgorithmFailurePolicy::PauseInstance)
    {
        if (instance->lifecycle == kernel::MechanismLifecycleState::Active)
        {
            commands.push_back(kernel::MechanismCommand::TransitionLifecycle(
                invocation.target,
                kernel::MechanismLifecycleState::Paused
            ));
        }
        else if (instance->lifecycle
            == kernel::MechanismLifecycleState::Created)
        {
            commands.push_back(kernel::MechanismCommand::TransitionLifecycle(
                invocation.target,
                kernel::MechanismLifecycleState::Failed
            ));
        }
    }
    else if (invocation.failurePolicy
            == kernel::AlgorithmFailurePolicy::FailInstance
        && !kernel::IsTerminalMechanismLifecycleState(instance->lifecycle))
    {
        commands.push_back(kernel::MechanismCommand::TransitionLifecycle(
            invocation.target,
            kernel::MechanismLifecycleState::Failed
        ));
    }

    const kernel::WorldTransactionResult isolated = ApplyImmediateCore(
        kernel::WorldTransaction::FromMechanismCommands(
            std::move(commands)
        ),
        currentTick
    );
    if (!isolated)
    {
        invocation.message +=
            "; Algorithm fault isolation transaction was rejected";
    }
}

}
