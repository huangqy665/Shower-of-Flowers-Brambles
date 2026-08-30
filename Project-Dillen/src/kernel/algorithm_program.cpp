#include "algorithm_program.hpp"

#include <utility>

#include "algorithm_registry.hpp"

namespace dillen::kernel {

namespace {

bool IsSingleEntryPoint(AlgorithmEntryPoint entryPoint) noexcept
{
    const std::uint32_t value = static_cast<std::uint32_t>(entryPoint);
    return value != 0 && (value & (value - 1U)) == 0;
}

}

AlgorithmInstructionDefinition AlgorithmInstructionDefinition::SetField(
    std::string field,
    MechanismValue value
)
{
    AlgorithmInstructionDefinition instruction;
    instruction.kind = AlgorithmInstructionKind::SetField;
    instruction.field = std::move(field);
    instruction.operand = std::move(value);
    return instruction;
}

AlgorithmInstructionDefinition AlgorithmInstructionDefinition::AddField(
    std::string field,
    MechanismValue value
)
{
    AlgorithmInstructionDefinition instruction;
    instruction.kind = AlgorithmInstructionKind::AddField;
    instruction.field = std::move(field);
    instruction.operand = std::move(value);
    return instruction;
}

AlgorithmInstructionDefinition
AlgorithmInstructionDefinition::TransitionLifecycle(
    MechanismLifecycleState lifecycle
)
{
    AlgorithmInstructionDefinition instruction;
    instruction.kind = AlgorithmInstructionKind::TransitionLifecycle;
    instruction.lifecycle = lifecycle;
    return instruction;
}

const std::vector<AlgorithmBytecodeInstruction>*
CompiledAlgorithmProgram::FindStage(
    AlgorithmEntryPoint entryPoint
) const
{
    const auto iterator = stages.find(entryPoint);
    return iterator == stages.end() ? nullptr : &iterator->second;
}

bool IsValidAlgorithmInstruction(
    const AlgorithmInstructionDefinition& instruction
) noexcept
{
    switch (instruction.kind)
    {
    case AlgorithmInstructionKind::SetField:
        if (instruction.field.empty()) return false;
        break;
    case AlgorithmInstructionKind::AddField:
        if (instruction.field.empty()
            || (!instruction.operandFromPayload
                && instruction.operand.Kind() != MechanismValueKind::Integer
                && instruction.operand.Kind() != MechanismValueKind::Decimal))
        {
            return false;
        }
        break;
    case AlgorithmInstructionKind::TransitionLifecycle:
        break;
    case AlgorithmInstructionKind::CreateEntity:
        if (!instruction.entityDefinition) return false;
        break;
    case AlgorithmInstructionKind::SetComponentField:
        if (!instruction.entity
            || !instruction.component
            || instruction.componentField.empty()) return false;
        break;
    case AlgorithmInstructionKind::AddRelation:
        if (!instruction.relationType
            || !instruction.sourceEntity
            || !instruction.targetEntity) return false;
        break;
    case AlgorithmInstructionKind::RemoveRelation:
        if (!instruction.relation) return false;
        break;
    case AlgorithmInstructionKind::SpawnMechanism:
        if (!instruction.spawn) return false;
        break;
    case AlgorithmInstructionKind::ScheduleEvent:
        if (!instruction.eventType
            || instruction.dueTickOffset == 0) return false;
        break;
    case AlgorithmInstructionKind::CancelEvent:
        if (instruction.eventSequence == 0) return false;
        break;
    case AlgorithmInstructionKind::CreateRngStream:
        if (!instruction.rngStream) return false;
        break;
    case AlgorithmInstructionKind::AdvanceRngStream:
        if (!instruction.rngStream
            || instruction.rngCount == 0) return false;
        break;
    case AlgorithmInstructionKind::InvokeCapability:
        if (instruction.capabilityName.empty()
            || instruction.dueTickOffset == 0
            || !instruction.capabilityVersions.IsValid()) return false;
        break;
    }
    for (const AlgorithmConditionDefinition& condition
        : instruction.conditions)
    {
        if ((condition.kind == AlgorithmConditionKind::SelfFieldEquals
                && condition.field.empty())
            || (condition.kind == AlgorithmConditionKind::QueryCountAtLeast
                && condition.queryType == 0)
            || (condition.kind
                    == AlgorithmConditionKind::ScheduledEventTypeEquals
                && !condition.eventType)
            || (condition.kind == AlgorithmConditionKind::RngModuloEquals
                && (!condition.rngStream
                    || condition.rngModulo == 0
                    || condition.rngEquals >= condition.rngModulo)))
        {
            return false;
        }
    }
    return true;
}

bool IsValidAlgorithmProgram(
    const AlgorithmProgramDefinition& program,
    AlgorithmEntryPoint entryPoints
) noexcept
{
    std::uint32_t declared = 0;
    for (const auto& stage : program.stages)
    {
        if (!IsSingleEntryPoint(stage.first)
            || !HasAlgorithmEntryPoint(entryPoints, stage.first))
        {
            return false;
        }
        declared |= static_cast<std::uint32_t>(stage.first);
        for (const AlgorithmInstructionDefinition& instruction
            : stage.second)
        {
            if (!IsValidAlgorithmInstruction(instruction))
            {
                return false;
            }
        }
    }
    return !program.stages.empty()
        && declared == static_cast<std::uint32_t>(entryPoints);
}

}
