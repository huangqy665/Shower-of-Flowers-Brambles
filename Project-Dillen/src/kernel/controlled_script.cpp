#include "controlled_script.hpp"

#include <algorithm>
#include <limits>
#include <set>

#include "mechanism_ids.hpp"

namespace dillen::kernel {

namespace {

bool IsSingleEntryPoint(AlgorithmEntryPoint entryPoint) noexcept
{
    const std::uint32_t value = static_cast<std::uint32_t>(entryPoint);
    return value != 0 && (value & (value - 1U)) == 0;
}

bool AddSize(std::size_t value, std::size_t& total) noexcept
{
    if (value > std::numeric_limits<std::size_t>::max() - total)
    {
        total = std::numeric_limits<std::size_t>::max();
        return false;
    }
    total += value;
    return true;
}

bool ValueFootprint(
    const MechanismValue& value,
    std::size_t& total,
    std::size_t depth
) noexcept
{
    if (depth > 64 || !AddSize(1, total))
    {
        return false;
    }
    switch (value.Kind())
    {
    case MechanismValueKind::Null:
        return true;
    case MechanismValueKind::Boolean:
        return AddSize(1, total);
    case MechanismValueKind::Integer:
    case MechanismValueKind::Decimal:
        return AddSize(8, total);
    case MechanismValueKind::String:
        return AddSize(
            std::get<std::string>(value.data).size(),
            total
        );
    case MechanismValueKind::Reference:
        return AddSize(17, total);
    case MechanismValueKind::List:
        for (const MechanismValue& entry
            : std::get<MechanismValue::List>(value.data))
        {
            if (!ValueFootprint(entry, total, depth + 1)) return false;
        }
        return true;
    case MechanismValueKind::Object:
        for (const auto& entry
            : std::get<MechanismValue::Object>(value.data))
        {
            if (!AddSize(entry.first.size(), total)
                || !ValueFootprint(entry.second, total, depth + 1))
            {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool NumericOperand(const MechanismValue& value) noexcept
{
    return value.Kind() == MechanismValueKind::Integer
        || value.Kind() == MechanismValueKind::Decimal;
}

}

const std::vector<ControlledScriptInstruction>*
CompiledControlledScriptProgram::FindStage(
    AlgorithmEntryPoint entryPoint
) const
{
    const auto iterator = stages.find(entryPoint);
    return iterator == stages.end() ? nullptr : &iterator->second;
}

bool operator==(
    const ControlledScriptContinuation& first,
    const ControlledScriptContinuation& second
) noexcept
{
    return first.entryPoint == second.entryPoint
        && first.programCounter == second.programCounter;
}

bool operator!=(
    const ControlledScriptContinuation& first,
    const ControlledScriptContinuation& second
) noexcept
{
    return !(first == second);
}

bool IsValidControlledScriptProgram(
    const ControlledScriptProgramDefinition& program,
    AlgorithmEntryPoint entryPoints
) noexcept
{
    std::set<std::string> stateNames;
    for (const ControlledScriptStateDefinition& state : program.state)
    {
        if (!IsValidMechanismSymbol(state.name)
            || state.name != NormalizeMechanismSymbol(state.name)
            || !stateNames.insert(state.name).second)
        {
            return false;
        }
    }

    std::uint32_t declared = 0;
    for (const auto& stage : program.stages)
    {
        if (!IsSingleEntryPoint(stage.first)
            || !HasAlgorithmEntryPoint(entryPoints, stage.first)
            || stage.second.empty())
        {
            return false;
        }
        declared |= static_cast<std::uint32_t>(stage.first);
        for (const ControlledScriptInstructionDefinition& instruction
            : stage.second)
        {
            switch (instruction.kind)
            {
            case ControlledScriptInstructionKind::SetState:
                if (stateNames.find(instruction.state) == stateNames.end())
                    return false;
                break;
            case ControlledScriptInstructionKind::AddState:
                if (stateNames.find(instruction.state) == stateNames.end()
                    || !NumericOperand(instruction.operand)) return false;
                break;
            case ControlledScriptInstructionKind::SetField:
                if (instruction.field.empty()) return false;
                break;
            case ControlledScriptInstructionKind::AddField:
                if (instruction.field.empty()
                    || !NumericOperand(instruction.operand)) return false;
                break;
            case ControlledScriptInstructionKind::TransitionLifecycle:
            case ControlledScriptInstructionKind::Yield:
            case ControlledScriptInstructionKind::Halt:
                break;
            case ControlledScriptInstructionKind::Jump:
                if (instruction.targetInstruction > stage.second.size())
                    return false;
                break;
            case ControlledScriptInstructionKind::JumpIfStateEquals:
                if (stateNames.find(instruction.state) == stateNames.end()
                    || instruction.targetInstruction > stage.second.size())
                {
                    return false;
                }
                break;
            case ControlledScriptInstructionKind::Transact:
                if (!IsValidAlgorithmInstruction(instruction.action))
                    return false;
                break;
            }
        }
    }
    return !program.stages.empty()
        && declared == static_cast<std::uint32_t>(entryPoints);
}

std::size_t ControlledScriptStateFootprint(
    const std::vector<MechanismValue>& state
) noexcept
{
    std::size_t total = 0;
    for (const MechanismValue& value : state)
    {
        if (!ValueFootprint(value, total, 0))
        {
            return std::numeric_limits<std::size_t>::max();
        }
    }
    return total;
}

bool IsValidControlledScriptRuntimeState(
    const CompiledControlledScriptProgram& program,
    const std::vector<MechanismValue>& state,
    const std::vector<ControlledScriptContinuation>& continuations,
    std::size_t memoryLimitBytes
) noexcept
{
    if (state.size() != program.initialState.size()
        || ControlledScriptStateFootprint(state) > memoryLimitBytes)
    {
        return false;
    }
    for (std::size_t index = 0; index < state.size(); ++index)
    {
        if (state[index].Kind() != program.initialState[index].Kind())
        {
            return false;
        }
    }
    std::set<std::uint32_t> stages;
    for (const ControlledScriptContinuation& continuation : continuations)
    {
        const auto* stage = program.FindStage(continuation.entryPoint);
        const std::uint32_t entry = static_cast<std::uint32_t>(
            continuation.entryPoint
        );
        if (stage == nullptr
            || continuation.programCounter >= stage->size()
            || !stages.insert(entry).second)
        {
            return false;
        }
    }
    return true;
}

}
