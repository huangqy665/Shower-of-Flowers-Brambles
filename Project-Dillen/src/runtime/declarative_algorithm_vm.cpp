#include "declarative_algorithm_vm.hpp"

#include "algorithm_runtime.hpp"
#include "bytecode_transaction.hpp"

#include <utility>

namespace dillen::runtime {

namespace {

DeclarativeAlgorithmResult Failure(
    DeclarativeAlgorithmStatus status,
    std::string message
)
{
    DeclarativeAlgorithmResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

DeclarativeAlgorithmStatus TranslateStatus(BytecodeTransactionStatus status)
{
    switch (status)
    {
    case BytecodeTransactionStatus::Ok:
        return DeclarativeAlgorithmStatus::Completed;
    case BytecodeTransactionStatus::InvalidFieldSlot:
        return DeclarativeAlgorithmStatus::InvalidFieldSlot;
    case BytecodeTransactionStatus::OperandTypeMismatch:
        return DeclarativeAlgorithmStatus::OperandTypeMismatch;
    case BytecodeTransactionStatus::NumericOverflow:
        return DeclarativeAlgorithmStatus::NumericOverflow;
    case BytecodeTransactionStatus::LifecycleTransitionRejected:
        return DeclarativeAlgorithmStatus::LifecycleTransitionRejected;
    }
    return DeclarativeAlgorithmStatus::OperandTypeMismatch;
}

}

DeclarativeAlgorithmResult::operator bool() const noexcept
{
    return status == DeclarativeAlgorithmStatus::Completed;
}

DeclarativeAlgorithmResult DeclarativeAlgorithmVm::Execute(
    const kernel::CompiledAlgorithmProgram& program,
    kernel::AlgorithmEntryPoint entryPoint,
    const kernel::MechanismInstance& instance,
    AlgorithmExecutionBudget& budget
) const
{
    WorldQuerySnapshot query;
    kernel::MechanismQuerySnapshot mechanisms;
    kernel::DeterministicRngSnapshot rng;
    kernel::FrozenRuntimeCatalog catalog;
    const std::vector<kernel::CapabilityBindingSlotId> capabilities;
    const AlgorithmInvocationContext context{
        AlgorithmRuntimeStage::Tick,
        0,
        instance,
        query,
        mechanisms,
        rng,
        catalog,
        capabilities,
        nullptr,
        nullptr,
        nullptr,
        budget
    };
    return Execute(program, entryPoint, context);
}

DeclarativeAlgorithmResult DeclarativeAlgorithmVm::Execute(
    const kernel::CompiledAlgorithmProgram& program,
    kernel::AlgorithmEntryPoint entryPoint,
    const AlgorithmInvocationContext& context
) const
{
    using namespace kernel;
    const MechanismInstance& instance = context.instance;
    AlgorithmExecutionBudget& budget = context.budget;
    const std::vector<AlgorithmBytecodeInstruction>* stage =
        program.FindStage(entryPoint);
    if (stage == nullptr)
    {
        return Failure(
            DeclarativeAlgorithmStatus::StageMissing,
            "Compiled declarative stage is missing"
        );
    }

    std::vector<MechanismValue> values = instance.values;
    MechanismLifecycleState lifecycle = instance.lifecycle;
    std::vector<WorldCommand> resultCommands;
    resultCommands.reserve(stage->size());
    for (const AlgorithmBytecodeInstruction& instruction : *stage)
    {
        if (!budget.Consume())
        {
            return Failure(
                DeclarativeAlgorithmStatus::InstructionBudgetExceeded,
                "Declarative instruction budget was exceeded"
            );
        }
        if (!EvaluateBytecodeConditions(instruction, context, values))
        {
            continue;
        }
        BytecodeTransactionOutcome outcome = EmitBytecodeTransaction(
            instruction,
            context,
            instance,
            values,
            lifecycle
        );
        if (!outcome)
        {
            return Failure(
                TranslateStatus(outcome.status),
                std::move(outcome.message)
            );
        }
        for (WorldCommand& command : outcome.commands)
        {
            resultCommands.push_back(std::move(command));
        }
    }

    DeclarativeAlgorithmResult result;
    result.transaction.commands = std::move(resultCommands);
    return result;
}

}
