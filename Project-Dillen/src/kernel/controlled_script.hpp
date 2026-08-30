#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "algorithm_program.hpp"
#include "mechanism_lifecycle.hpp"
#include "mechanism_value.hpp"

namespace dillen::kernel {

enum class ControlledScriptInstructionKind
{
    SetState,
    AddState,
    SetField,
    AddField,
    TransitionLifecycle,
    Jump,
    JumpIfStateEquals,
    Yield,
    Halt,
    // Wraps a declarative transaction instruction (entity / component /
    // relation / spawn / scheduled event / RNG / capability / field mutation,
    // with optional `when` conditions). Lowered and executed through the same
    // shared code path the declarative VM uses -- see `action`.
    Transact
};

struct ControlledScriptStateDefinition
{
    std::string name;
    MechanismValue initialValue;
};

struct ControlledScriptInstructionDefinition
{
    ControlledScriptInstructionKind kind =
        ControlledScriptInstructionKind::Halt;
    std::string state;
    std::string field;
    MechanismValue operand;
    MechanismLifecycleState lifecycle =
        MechanismLifecycleState::Created;
    std::uint32_t targetInstruction = 0;
    // Only used when kind == Transact.
    AlgorithmInstructionDefinition action;
};

struct ControlledScriptProgramDefinition
{
    std::vector<ControlledScriptStateDefinition> state;
    std::map<
        AlgorithmEntryPoint,
        std::vector<ControlledScriptInstructionDefinition>
    > stages;
};

enum class ControlledScriptOpcode
{
    SetStateConstant,
    AddStateIntegerConstant,
    AddStateDecimalConstant,
    SetFieldConstant,
    AddFieldIntegerConstant,
    AddFieldDecimalConstant,
    TransitionLifecycle,
    Jump,
    JumpIfStateEquals,
    Yield,
    Halt,
    Transact
};

struct ControlledScriptInstruction
{
    ControlledScriptOpcode opcode = ControlledScriptOpcode::Halt;
    std::uint32_t stateSlot = 0;
    MechanismFieldSlotId field;
    MechanismValue operand;
    MechanismLifecycleState lifecycle =
        MechanismLifecycleState::Created;
    std::uint32_t targetInstruction = 0;
    // Only used when opcode == Transact: the fully lowered declarative
    // bytecode instruction, run through the shared transaction emitter.
    AlgorithmBytecodeInstruction transact;
};

struct CompiledControlledScriptProgram
{
    MechanismDefinitionId definition;
    AlgorithmId algorithm;
    std::uint32_t algorithmVersion = 0;
    std::vector<MechanismValue> initialState;
    std::map<std::string, std::uint32_t> stateSlotsByName;
    std::map<
        AlgorithmEntryPoint,
        std::vector<ControlledScriptInstruction>
    > stages;

    const std::vector<ControlledScriptInstruction>* FindStage(
        AlgorithmEntryPoint entryPoint
    ) const;
};

struct ControlledScriptContinuation
{
    AlgorithmEntryPoint entryPoint = AlgorithmEntryPoint::None;
    std::uint32_t programCounter = 0;
};

bool operator==(
    const ControlledScriptContinuation& first,
    const ControlledScriptContinuation& second
) noexcept;
bool operator!=(
    const ControlledScriptContinuation& first,
    const ControlledScriptContinuation& second
) noexcept;

bool IsValidControlledScriptProgram(
    const ControlledScriptProgramDefinition& program,
    AlgorithmEntryPoint entryPoints
) noexcept;

std::size_t ControlledScriptStateFootprint(
    const std::vector<MechanismValue>& state
) noexcept;

bool IsValidControlledScriptRuntimeState(
    const CompiledControlledScriptProgram& program,
    const std::vector<MechanismValue>& state,
    const std::vector<ControlledScriptContinuation>& continuations,
    std::size_t memoryLimitBytes
) noexcept;

}
