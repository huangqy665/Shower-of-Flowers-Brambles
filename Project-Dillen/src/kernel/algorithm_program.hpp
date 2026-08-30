#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "mechanism_ids.hpp"
#include "mechanism_lifecycle.hpp"
#include "mechanism_value.hpp"
#include "runtime_capability_contract.hpp"

namespace dillen::kernel {

enum class AlgorithmEntryPoint : std::uint32_t
{
    None = 0,
    Create = 1U << 0U,
    Tick = 1U << 1U,
    Event = 1U << 2U,
    Command = 1U << 3U,
    Destroy = 1U << 4U
};

AlgorithmEntryPoint operator|(
    AlgorithmEntryPoint first,
    AlgorithmEntryPoint second
) noexcept;
bool HasAlgorithmEntryPoint(
    AlgorithmEntryPoint value,
    AlgorithmEntryPoint flag
) noexcept;

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
    AdvanceRngStream,
    InvokeCapability
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
    std::string capabilityName;
    bool operandFromPayload = false;
    // invoke_capability: empty targetRoleName = broadcast to every provider;
    // a role name = deliver only to the instance bound to that role slot on
    // the invoking mechanism. capabilityVersions is the requested contract
    // version range ({1, open} when the author omits `version`).
    std::string targetRoleName;
    CapabilityVersionRange capabilityVersions;

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
    AdvanceRngStream,
    InvokeCapability
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
    CapabilityId capability;
    AlgorithmEventTypeId capabilityDeliveryType;
    bool operandFromPayload = false;
    // invoke_capability: empty targetRoleSlot = broadcast; a resolved role slot
    // = deliver only to the instance in that slot on the invoking mechanism.
    // capabilityVersion is the concrete contract version the compiler resolved.
    MechanismRoleSlotId targetRoleSlot;
    std::uint32_t capabilityVersion = 0;
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

// Per-instruction shape check (field/operand/id presence, condition operands).
// Does not resolve names against a catalog -- that is the Runtime Compiler's
// job. Shared by IsValidAlgorithmProgram and the controlled-script validator.
bool IsValidAlgorithmInstruction(
    const AlgorithmInstructionDefinition& instruction
) noexcept;

bool IsValidAlgorithmProgram(
    const AlgorithmProgramDefinition& program,
    AlgorithmEntryPoint entryPoints
) noexcept;

}
