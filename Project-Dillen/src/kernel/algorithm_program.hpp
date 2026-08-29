#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "mechanism_ids.hpp"
#include "mechanism_lifecycle.hpp"
#include "mechanism_value.hpp"

namespace dillen::kernel {

enum class AlgorithmEntryPoint : std::uint32_t;

enum class AlgorithmInstructionKind
{
    SetField,
    AddField,
    TransitionLifecycle,
    CreateEntity,
    SetComponentField,
    AddRelation,
    RemoveRelation,
    SpawnMechanism,
    ScheduleEvent,
    CancelEvent,
    CreateRngStream,
    AdvanceRngStream
};

enum class AlgorithmQueryKind
{
    EntityType,
    ComponentType,
    RelationType,
    MechanismType
};

enum class AlgorithmConditionKind
{
    SelfFieldEquals,
    QueryCountAtLeast,
    ScheduledEventTypeEquals,
    RngModuloEquals
};

struct AlgorithmConditionDefinition
{
    AlgorithmConditionKind kind = AlgorithmConditionKind::SelfFieldEquals;
    std::string field;
    MechanismValue value;
    AlgorithmQueryKind queryKind = AlgorithmQueryKind::EntityType;
    std::uint64_t queryType = 0;
    std::uint64_t minimumCount = 0;
    AlgorithmEventTypeId eventType;
    RngStreamId rngStream;
    std::uint64_t rngOffset = 0;
    std::uint64_t rngModulo = 1;
    std::uint64_t rngEquals = 0;
};

struct AlgorithmInstructionDefinition
{
    AlgorithmInstructionKind kind = AlgorithmInstructionKind::SetField;
    std::string field;
    MechanismValue operand;
    MechanismLifecycleState lifecycle = MechanismLifecycleState::Created;
    std::vector<AlgorithmConditionDefinition> conditions;
    EntityDefinitionId entityDefinition;
    EntityId entity;
    ComponentTypeId component;
    std::string componentField;
    RelationTypeId relationType;
    EntityId sourceEntity;
    EntityId targetEntity;
    RelationId relation;
    MechanismSpawnDefinitionId spawn;
    AlgorithmEventTypeId eventType;
    std::uint64_t dueTickOffset = 1;
    std::int32_t priority = 0;
    MechanismValue payload;
    std::uint64_t eventSequence = 0;
    RngStreamId rngStream;
    std::uint64_t rngSeed = 0;
    std::uint64_t rngCount = 0;

    static AlgorithmInstructionDefinition SetField(
        std::string field,
        MechanismValue value
    );
    static AlgorithmInstructionDefinition AddField(
        std::string field,
        MechanismValue value
    );
    static AlgorithmInstructionDefinition TransitionLifecycle(
        MechanismLifecycleState lifecycle
    );
};

struct AlgorithmProgramDefinition
{
    std::map<
        AlgorithmEntryPoint,
        std::vector<AlgorithmInstructionDefinition>
    > stages;
};

enum class AlgorithmBytecodeOpcode
{
    SetFieldConstant,
    AddIntegerConstant,
    AddDecimalConstant,
    TransitionLifecycle,
    CreateEntity,
    SetComponentFieldConstant,
    AddRelation,
    RemoveRelation,
    SpawnMechanism,
    ScheduleEvent,
    CancelEvent,
    CreateRngStream,
    AdvanceRngStream
};

struct CompiledAlgorithmCondition
{
    AlgorithmConditionKind kind = AlgorithmConditionKind::SelfFieldEquals;
    MechanismFieldSlotId field;
    MechanismValue value;
    AlgorithmQueryKind queryKind = AlgorithmQueryKind::EntityType;
    std::uint64_t queryType = 0;
    std::uint64_t minimumCount = 0;
    AlgorithmEventTypeId eventType;
    RngStreamId rngStream;
    std::uint64_t rngOffset = 0;
    std::uint64_t rngModulo = 1;
    std::uint64_t rngEquals = 0;
};

struct AlgorithmBytecodeInstruction
{
    AlgorithmBytecodeOpcode opcode =
        AlgorithmBytecodeOpcode::SetFieldConstant;
    MechanismFieldSlotId field;
    MechanismValue operand;
    MechanismLifecycleState lifecycle = MechanismLifecycleState::Created;
    std::vector<CompiledAlgorithmCondition> conditions;
    EntityDefinitionId entityDefinition;
    EntityId entity;
    ComponentTypeId component;
    ComponentFieldSlotId componentField;
    RelationTypeId relationType;
    EntityId sourceEntity;
    EntityId targetEntity;
    RelationId relation;
    MechanismSpawnDefinitionId spawn;
    AlgorithmEventTypeId eventType;
    std::uint64_t dueTickOffset = 1;
    std::int32_t priority = 0;
    MechanismValue payload;
    std::uint64_t eventSequence = 0;
    RngStreamId rngStream;
    std::uint64_t rngSeed = 0;
    std::uint64_t rngCount = 0;
};

struct CompiledAlgorithmProgram
{
    MechanismDefinitionId definition;
    AlgorithmId algorithm;
    std::uint32_t algorithmVersion = 0;
    std::map<
        AlgorithmEntryPoint,
        std::vector<AlgorithmBytecodeInstruction>
    > stages;

    const std::vector<AlgorithmBytecodeInstruction>* FindStage(
        AlgorithmEntryPoint entryPoint
    ) const;
};

bool IsValidAlgorithmProgram(
    const AlgorithmProgramDefinition& program,
    AlgorithmEntryPoint entryPoints
) noexcept;

}
