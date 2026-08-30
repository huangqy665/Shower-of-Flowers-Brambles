#include "controlled_script_vm.hpp"

#include "bytecode_transaction.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace dillen::runtime {

namespace {

ControlledScriptResult Failure(
    ControlledScriptStatus status,
    std::string message
)
{
    ControlledScriptResult result;
    result.status = status;
    result.message = std::move(message);
    return result;
}

bool AddInteger(
    kernel::MechanismValue& target,
    const kernel::MechanismValue& operand
) noexcept
{
    const std::int64_t first = std::get<std::int64_t>(target.data);
    const std::int64_t second = std::get<std::int64_t>(operand.data);
    if ((second > 0
            && first > std::numeric_limits<std::int64_t>::max() - second)
        || (second < 0
            && first < std::numeric_limits<std::int64_t>::min() - second))
    {
        return false;
    }
    target = kernel::MechanismValue(first + second);
    return true;
}

bool AddDecimal(
    kernel::MechanismValue& target,
    const kernel::MechanismValue& operand
) noexcept
{
    const double value = std::get<double>(target.data)
        + std::get<double>(operand.data);
    if (!std::isfinite(value)) return false;
    target = kernel::MechanismValue(value);
    return true;
}

auto FindContinuation(
    std::vector<kernel::ControlledScriptContinuation>& continuations,
    kernel::AlgorithmEntryPoint entryPoint
)
{
    return std::find_if(
        continuations.begin(),
        continuations.end(),
        [entryPoint](const kernel::ControlledScriptContinuation& value)
        {
            return value.entryPoint == entryPoint;
        }
    );
}

void SetContinuation(
    std::vector<kernel::ControlledScriptContinuation>& continuations,
    kernel::AlgorithmEntryPoint entryPoint,
    std::uint32_t programCounter
)
{
    auto iterator = FindContinuation(continuations, entryPoint);
    if (iterator == continuations.end())
    {
        continuations.push_back({entryPoint, programCounter});
    }
    else
    {
        iterator->programCounter = programCounter;
    }
    std::sort(
        continuations.begin(),
        continuations.end(),
        [](const kernel::ControlledScriptContinuation& first,
           const kernel::ControlledScriptContinuation& second)
        {
            return static_cast<std::uint32_t>(first.entryPoint)
                < static_cast<std::uint32_t>(second.entryPoint);
        }
    );
}

void ClearContinuation(
    std::vector<kernel::ControlledScriptContinuation>& continuations,
    kernel::AlgorithmEntryPoint entryPoint
)
{
    const auto iterator = FindContinuation(continuations, entryPoint);
    if (iterator != continuations.end()) continuations.erase(iterator);
}

void AppendRuntimeState(
    ControlledScriptResult& result,
    const kernel::MechanismInstance& instance,
    std::vector<kernel::MechanismValue> state,
    std::vector<kernel::ControlledScriptContinuation> continuations
)
{
    if (state == instance.algorithmState
        && continuations == instance.algorithmContinuations)
    {
        return;
    }
    result.transaction.commands.push_back(kernel::WorldCommand::Mechanism(
        kernel::MechanismCommand::ReplaceAlgorithmState(
            instance.id,
            std::move(state),
            std::move(continuations)
        )
    ));
}

}

ControlledScriptResult::operator bool() const noexcept
{
    return status == ControlledScriptStatus::Completed
        || status == ControlledScriptStatus::Preempted;
}

ControlledScriptResult ControlledScriptVm::Execute(
    const kernel::CompiledControlledScriptProgram& program,
    kernel::AlgorithmEntryPoint entryPoint,
    const AlgorithmInvocationContext& context,
    const kernel::AlgorithmExecutionPolicy& policy
) const
{
    const std::vector<kernel::ControlledScriptInstruction>* stage =
        program.FindStage(entryPoint);
    if (stage == nullptr)
    {
        return Failure(
            ControlledScriptStatus::ExecutionRejected,
            "Controlled Script stage is missing"
        );
    }
    if (!kernel::IsValidControlledScriptRuntimeState(
            program,
            context.instance.algorithmState,
            context.instance.algorithmContinuations,
            policy.scriptMemoryLimitBytes))
    {
        return Failure(
            ControlledScriptStatus::RuntimeStateInvalid,
            "Controlled Script authority state is invalid"
        );
    }

    std::vector<kernel::MechanismValue> state =
        context.instance.algorithmState;
    std::vector<kernel::MechanismValue> fields = context.instance.values;
    kernel::MechanismLifecycleState lifecycle = context.instance.lifecycle;
    std::vector<kernel::ControlledScriptContinuation> continuations =
        context.instance.algorithmContinuations;
    const auto continuation = FindContinuation(continuations, entryPoint);
    std::uint32_t programCounter = continuation == continuations.end()
        ? 0
        : continuation->programCounter;
    std::uint32_t sliceInstructions = 0;
    ControlledScriptResult result;

    while (programCounter < stage->size())
    {
        if (sliceInstructions >= policy.scriptSliceInstructionBudget)
        {
            SetContinuation(continuations, entryPoint, programCounter);
            result.status = ControlledScriptStatus::Preempted;
            AppendRuntimeState(
                result,
                context.instance,
                std::move(state),
                std::move(continuations)
            );
            return result;
        }
        if (!context.budget.Consume())
        {
            return Failure(
                ControlledScriptStatus::InstructionBudgetExceeded,
                "Controlled Script instruction budget was exceeded"
            );
        }
        ++sliceInstructions;

        const kernel::ControlledScriptInstruction& instruction =
            (*stage)[programCounter];
        std::uint32_t next = programCounter + 1;
        switch (instruction.opcode)
        {
        case kernel::ControlledScriptOpcode::SetStateConstant:
            state[instruction.stateSlot] = instruction.operand;
            break;
        case kernel::ControlledScriptOpcode::AddStateIntegerConstant:
            if (!AddInteger(state[instruction.stateSlot], instruction.operand))
            {
                return Failure(
                    ControlledScriptStatus::ExecutionRejected,
                    "Controlled Script integer state overflow"
                );
            }
            break;
        case kernel::ControlledScriptOpcode::AddStateDecimalConstant:
            if (!AddDecimal(state[instruction.stateSlot], instruction.operand))
            {
                return Failure(
                    ControlledScriptStatus::ExecutionRejected,
                    "Controlled Script decimal state overflow"
                );
            }
            break;
        case kernel::ControlledScriptOpcode::SetFieldConstant:
            fields[instruction.field.value] = instruction.operand;
            result.transaction.commands.push_back(
                kernel::WorldCommand::Mechanism(
                    kernel::MechanismCommand::SetField(
                        context.instance.id,
                        instruction.field,
                        instruction.operand
                    )
                )
            );
            break;
        case kernel::ControlledScriptOpcode::AddFieldIntegerConstant:
            if (!AddInteger(
                    fields[instruction.field.value],
                    instruction.operand))
            {
                return Failure(
                    ControlledScriptStatus::ExecutionRejected,
                    "Controlled Script integer field overflow"
                );
            }
            result.transaction.commands.push_back(
                kernel::WorldCommand::Mechanism(
                    kernel::MechanismCommand::SetField(
                        context.instance.id,
                        instruction.field,
                        fields[instruction.field.value]
                    )
                )
            );
            break;
        case kernel::ControlledScriptOpcode::AddFieldDecimalConstant:
            if (!AddDecimal(
                    fields[instruction.field.value],
                    instruction.operand))
            {
                return Failure(
                    ControlledScriptStatus::ExecutionRejected,
                    "Controlled Script decimal field overflow"
                );
            }
            result.transaction.commands.push_back(
                kernel::WorldCommand::Mechanism(
                    kernel::MechanismCommand::SetField(
                        context.instance.id,
                        instruction.field,
                        fields[instruction.field.value]
                    )
                )
            );
            break;
        case kernel::ControlledScriptOpcode::TransitionLifecycle:
            result.transaction.commands.push_back(
                kernel::WorldCommand::Mechanism(
                    kernel::MechanismCommand::TransitionLifecycle(
                        context.instance.id,
                        instruction.lifecycle
                    )
                )
            );
            break;
        case kernel::ControlledScriptOpcode::Jump:
            next = instruction.targetInstruction;
            break;
        case kernel::ControlledScriptOpcode::JumpIfStateEquals:
            if (state[instruction.stateSlot] == instruction.operand)
            {
                next = instruction.targetInstruction;
            }
            break;
        case kernel::ControlledScriptOpcode::Yield:
            if (next < stage->size())
            {
                SetContinuation(continuations, entryPoint, next);
                result.status = ControlledScriptStatus::Preempted;
            }
            else
            {
                ClearContinuation(continuations, entryPoint);
            }
            AppendRuntimeState(
                result,
                context.instance,
                std::move(state),
                std::move(continuations)
            );
            return result;
        case kernel::ControlledScriptOpcode::Halt:
            ClearContinuation(continuations, entryPoint);
            AppendRuntimeState(
                result,
                context.instance,
                std::move(state),
                std::move(continuations)
            );
            return result;
        case kernel::ControlledScriptOpcode::Transact:
            if (EvaluateBytecodeConditions(
                    instruction.transact,
                    context,
                    fields))
            {
                BytecodeTransactionOutcome outcome = EmitBytecodeTransaction(
                    instruction.transact,
                    context,
                    context.instance,
                    fields,
                    lifecycle
                );
                if (!outcome)
                {
                    return Failure(
                        ControlledScriptStatus::ExecutionRejected,
                        std::move(outcome.message)
                    );
                }
                for (kernel::WorldCommand& command : outcome.commands)
                {
                    result.transaction.commands.push_back(std::move(command));
                }
            }
            break;
        }

        if (kernel::ControlledScriptStateFootprint(state)
            > policy.scriptMemoryLimitBytes)
        {
            return Failure(
                ControlledScriptStatus::MemoryQuotaExceeded,
                "Controlled Script memory quota was exceeded"
            );
        }
        programCounter = next;
    }

    ClearContinuation(continuations, entryPoint);
    AppendRuntimeState(
        result,
        context.instance,
        std::move(state),
        std::move(continuations)
    );
    return result;
}

}
