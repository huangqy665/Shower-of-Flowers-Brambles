#include "bytecode_transaction.hpp"

#include "algorithm_runtime.hpp"

#include <cmath>
#include <limits>
#include <utility>
#include <variant>

namespace dillen::runtime {

namespace {

BytecodeTransactionOutcome Reject(
    BytecodeTransactionStatus status,
    std::string message
)
{
    BytecodeTransactionOutcome outcome;
    outcome.status = status;
    outcome.message = std::move(message);
    return outcome;
}

bool AddWithoutOverflow(
    std::int64_t current,
    std::int64_t operand,
    std::int64_t& output
) noexcept
{
    if ((operand > 0
            && current > std::numeric_limits<std::int64_t>::max() - operand)
        || (operand < 0
            && current < std::numeric_limits<std::int64_t>::min() - operand))
    {
        return false;
    }
    output = current + operand;
    return true;
}

}

bool EvaluateBytecodeConditions(
    const kernel::AlgorithmBytecodeInstruction& instruction,
    const AlgorithmInvocationContext& context,
    const std::vector<kernel::MechanismValue>& values
)
{
    using namespace kernel;
    for (const CompiledAlgorithmCondition& condition
        : instruction.conditions)
    {
        switch (condition.kind)
        {
        case AlgorithmConditionKind::SelfFieldEquals:
            if (condition.field.value >= values.size()
                || values[condition.field.value] != condition.value)
            {
                return false;
            }
            break;
        case AlgorithmConditionKind::QueryCountAtLeast:
        {
            std::size_t count = 0;
            switch (condition.queryKind)
            {
            case AlgorithmQueryKind::EntityType:
                count = context.query.Entities().FindByType(
                    EntityTypeId{condition.queryType}
                ).size();
                break;
            case AlgorithmQueryKind::ComponentType:
                count = context.query.Components().FindOwners(
                    ComponentTypeId{condition.queryType}
                ).size();
                break;
            case AlgorithmQueryKind::RelationType:
                count = context.query.Relations().FindByType(
                    RelationTypeId{condition.queryType}
                ).size();
                break;
            case AlgorithmQueryKind::MechanismType:
                count = context.snapshot.FindByType(
                    MechanismTypeId{condition.queryType}
                ).size();
                break;
            }
            if (count < condition.minimumCount)
            {
                return false;
            }
            break;
        }
        case AlgorithmConditionKind::ScheduledEventTypeEquals:
            if (context.scheduledEvent == nullptr
                || context.scheduledEvent->type != condition.eventType)
            {
                return false;
            }
            break;
        case AlgorithmConditionKind::RngModuloEquals:
            if (context.rng.Find(condition.rngStream) == nullptr
                || context.rng.Preview(
                    condition.rngStream,
                    condition.rngOffset) % condition.rngModulo
                    != condition.rngEquals)
            {
                return false;
            }
            break;
        }
    }
    return true;
}

BytecodeTransactionOutcome EmitBytecodeTransaction(
    const kernel::AlgorithmBytecodeInstruction& instruction,
    const AlgorithmInvocationContext& context,
    const kernel::MechanismInstance& instance,
    std::vector<kernel::MechanismValue>& values,
    kernel::MechanismLifecycleState& lifecycle
)
{
    using namespace kernel;
    BytecodeTransactionOutcome outcome;
    std::vector<WorldCommand>& resultCommands = outcome.commands;

    if (instruction.opcode == AlgorithmBytecodeOpcode::TransitionLifecycle)
    {
        if (!CanTransitionMechanismLifecycle(
                lifecycle,
                instruction.lifecycle))
        {
            return Reject(
                BytecodeTransactionStatus::LifecycleTransitionRejected,
                "lifecycle transition is not permitted"
            );
        }
        lifecycle = instruction.lifecycle;
        resultCommands.push_back(WorldCommand::Mechanism(
            MechanismCommand::TransitionLifecycle(instance.id, lifecycle)
        ));
        return outcome;
    }
    if (instruction.opcode == AlgorithmBytecodeOpcode::CreateEntity)
    {
        resultCommands.push_back(WorldCommand::CreateEntity(
            instruction.entityDefinition
        ));
        return outcome;
    }
    if (instruction.opcode
        == AlgorithmBytecodeOpcode::SetComponentFieldConstant)
    {
        resultCommands.push_back(WorldCommand::SetComponentField(
            instruction.entity,
            instruction.component,
            instruction.componentField,
            instruction.operand
        ));
        return outcome;
    }
    if (instruction.opcode == AlgorithmBytecodeOpcode::AddRelation)
    {
        resultCommands.push_back(WorldCommand::AddRelation(
            instruction.relationType,
            instruction.sourceEntity,
            instruction.targetEntity
        ));
        return outcome;
    }
    if (instruction.opcode == AlgorithmBytecodeOpcode::RemoveRelation)
    {
        resultCommands.push_back(WorldCommand::RemoveRelation(
            instruction.relation
        ));
        return outcome;
    }
    if (instruction.opcode == AlgorithmBytecodeOpcode::SpawnMechanism)
    {
        resultCommands.push_back(WorldCommand::SpawnMechanism(
            instruction.spawn
        ));
        return outcome;
    }
    if (instruction.opcode == AlgorithmBytecodeOpcode::ScheduleEvent)
    {
        resultCommands.push_back(WorldCommand::ScheduleEvent(
            instruction.eventType,
            instance.id,
            context.tick + instruction.dueTickOffset,
            instruction.priority,
            instruction.payload
        ));
        return outcome;
    }
    if (instruction.opcode == AlgorithmBytecodeOpcode::InvokeCapability)
    {
        MechanismInstanceId targetInstance;
        if (instruction.targetRoleSlot)
        {
            const std::size_t roleSlot = instruction.targetRoleSlot.value;
            const MechanismReference* bound = nullptr;
            if (roleSlot < instance.roles.size())
            {
                for (const MechanismReference& reference
                    : instance.roles[roleSlot])
                {
                    if (reference.kind
                        == MechanismReferenceKind::MechanismInstance)
                    {
                        bound = &reference;
                        break;
                    }
                }
            }
            if (bound == nullptr)
            {
                return Reject(
                    BytecodeTransactionStatus::OperandTypeMismatch,
                    "invoke_capability target_role is not bound to a "
                    "mechanism instance"
                );
            }
            targetInstance = MechanismInstanceId{bound->value};
        }
        resultCommands.push_back(WorldCommand::InvokeCapability(
            instruction.capability,
            instruction.capabilityDeliveryType,
            context.tick + instruction.dueTickOffset,
            instruction.priority,
            instruction.payload,
            targetInstance,
            instruction.capabilityVersion
        ));
        return outcome;
    }
    if (instruction.opcode == AlgorithmBytecodeOpcode::CancelEvent)
    {
        resultCommands.push_back(WorldCommand::CancelEvent(
            instruction.eventSequence
        ));
        return outcome;
    }
    if (instruction.opcode == AlgorithmBytecodeOpcode::CreateRngStream)
    {
        resultCommands.push_back(WorldCommand::CreateRngStream(
            instruction.rngStream,
            instruction.rngSeed
        ));
        return outcome;
    }
    if (instruction.opcode == AlgorithmBytecodeOpcode::AdvanceRngStream)
    {
        const DeterministicRngStream* stream = context.rng.Find(
            instruction.rngStream
        );
        if (stream == nullptr)
        {
            return Reject(
                BytecodeTransactionStatus::OperandTypeMismatch,
                "RNG advance references a missing stream"
            );
        }
        resultCommands.push_back(WorldCommand::AdvanceRngStream(
            instruction.rngStream,
            stream->drawCount,
            instruction.rngCount
        ));
        return outcome;
    }
    if (instruction.field.value >= values.size())
    {
        return Reject(
            BytecodeTransactionStatus::InvalidFieldSlot,
            "bytecode field Slot is out of range"
        );
    }

    if (instruction.operandFromPayload && context.scheduledEvent == nullptr)
    {
        return Reject(
            BytecodeTransactionStatus::OperandTypeMismatch,
            "from_payload requires an active scheduled invocation"
        );
    }
    const MechanismValue& operandValue = instruction.operandFromPayload
        ? context.scheduledEvent->payload
        : instruction.operand;

    MechanismValue next;
    if (instruction.opcode == AlgorithmBytecodeOpcode::SetFieldConstant)
    {
        next = operandValue;
    }
    else if (instruction.opcode
        == AlgorithmBytecodeOpcode::AddIntegerConstant)
    {
        const auto* current = std::get_if<std::int64_t>(
            &values[instruction.field.value].data
        );
        const auto* operand = std::get_if<std::int64_t>(&operandValue.data);
        std::int64_t sum = 0;
        if (current == nullptr || operand == nullptr)
        {
            return Reject(
                BytecodeTransactionStatus::OperandTypeMismatch,
                "Integer bytecode requires an integer field and operand"
            );
        }
        if (!AddWithoutOverflow(*current, *operand, sum))
        {
            return Reject(
                BytecodeTransactionStatus::NumericOverflow,
                "Integer bytecode addition overflowed"
            );
        }
        next = MechanismValue(sum);
    }
    else
    {
        const auto* current = std::get_if<double>(
            &values[instruction.field.value].data
        );
        const auto* operand = std::get_if<double>(&operandValue.data);
        if (current == nullptr || operand == nullptr)
        {
            return Reject(
                BytecodeTransactionStatus::OperandTypeMismatch,
                "Decimal bytecode requires a decimal field and operand"
            );
        }
        const double sum = *current + *operand;
        if (!std::isfinite(sum))
        {
            return Reject(
                BytecodeTransactionStatus::NumericOverflow,
                "Decimal bytecode addition produced a non-finite value"
            );
        }
        next = MechanismValue(sum);
    }
    values[instruction.field.value] = next;
    resultCommands.push_back(WorldCommand::Mechanism(
        MechanismCommand::SetField(
            instance.id,
            instruction.field,
            std::move(next)
        )
    ));
    return outcome;
}

}
