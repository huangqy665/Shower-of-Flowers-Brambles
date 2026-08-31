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
    InvokeCapability,
    // Appended, never inserted. The compile golden proves that adding these
    // leaves every existing construct's lowering byte-identical.
    SetFieldComputed,
    AddFieldComputed
};

enum class AlgorithmQueryKind
{
    EntityType,
    ComponentType,
    RelationType,
    MechanismType
};

// Where a run-time operand is read from.
//
// Until now the only operands were a compile-time constant and the scheduled
// event payload, which is why the DSL could express "add 1" and nothing else:
// no instruction could read a value. These are the roots of a read path.
//
// A Mechanism Instance owns fields and role slots; it does NOT own Components
// -- those belong to Entities. So reaching a Component always starts at a role
// slot that references an Entity, and a Relation hop always starts from such an
// Entity. That ordering is forced by the data model, not chosen.
enum class AlgorithmReadRoot
{
    Constant,
    EventPayload,
    SelfField,
    RoleTarget
};

// A role slot holds a list of references, and a Relation hop widens that list
// again, so a path can name many values. The reducer says what a set of values
// means -- which is why aggregation is not a separate feature bolted on, but
// the multi-valued case of an ordinary read.
enum class AlgorithmReduce
{
    // Exactly one value must be reachable; anything else is a Fault. The
    // explicit scalar form, so "there happened to be two" never silently
    // becomes "the first one".
    RequireOne,
    Sum,
    Count,
    Minimum,
    Maximum
};

enum class AlgorithmRelationDirection
{
    Outgoing,
    Incoming
};

// Terminal read on whatever the path arrives at.
enum class AlgorithmReadTerminal
{
    // The path stops at the root value itself (Constant / EventPayload).
    Value,
    // Referenced Mechanism Instance's field slot.
    MechanismField,
    // Referenced Entity's Component field slot.
    ComponentField
};

struct AlgorithmReadPathDefinition
{
    AlgorithmReadRoot root = AlgorithmReadRoot::Constant;
    MechanismValue constant;
    std::string selfField;
    std::string role;
    bool traverseRelation = false;
    RelationTypeId relationType;
    AlgorithmRelationDirection direction =
        AlgorithmRelationDirection::Outgoing;
    AlgorithmReadTerminal terminal = AlgorithmReadTerminal::Value;
    ComponentTypeId component;
    std::string componentField;
    std::string targetField;
    AlgorithmReduce reduce = AlgorithmReduce::RequireOne;
};

enum class AlgorithmBinaryOperator
{
    Add,
    Subtract,
    Multiply,
    Divide,
    Minimum,
    Maximum
};

enum class AlgorithmCompareOperator
{
    Equal,
    NotEqual,
    Less,
    LessOrEqual,
    Greater,
    GreaterOrEqual
};

// FROZEN ORDER for the four original kinds -- they are the on-disk tags of
// nothing, but they are the compile golden's tags of everything. Append only.
enum class AlgorithmConditionKind
{
    SelfFieldEquals,
    QueryCountAtLeast,
    ScheduledEventTypeEquals,
    RngModuloEquals,
    // Both sides are read paths, so this subsumes SelfFieldEquals -- which is
    // kept because existing content compiles to it and must keep compiling to
    // exactly the same bytes.
    Compare
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
    // kind == Compare
    AlgorithmReadPathDefinition left;
    AlgorithmReadPathDefinition right;
    AlgorithmCompareOperator compare = AlgorithmCompareOperator::Equal;
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
    // kind == SetFieldComputed / AddFieldComputed
    AlgorithmReadPathDefinition left;
    AlgorithmReadPathDefinition right;
    bool hasRight = false;
    AlgorithmBinaryOperator binaryOperator = AlgorithmBinaryOperator::Add;

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

// FROZEN ORDER. These are the tags the compile golden pins, so an insertion or
// a reorder rewrites the lowering of content that did not change. Append only.
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
    InvokeCapability,
    // Appended 2026-08-31. The *Constant opcodes above stay exactly as they
    // are: existing content keeps lowering to them, which is what the compile
    // golden holding at 1617 bytes proves.
    SetFieldComputed,
    AddFieldComputed
};

// Slot-resolved read path. Names have become slots, so nothing here needs a
// string lookup at run time.
struct CompiledAlgorithmReadPath
{
    AlgorithmReadRoot root = AlgorithmReadRoot::Constant;
    MechanismValue constant;
    MechanismFieldSlotId selfField;
    MechanismRoleSlotId role;
    bool traverseRelation = false;
    RelationTypeId relationType;
    AlgorithmRelationDirection direction =
        AlgorithmRelationDirection::Outgoing;
    AlgorithmReadTerminal terminal = AlgorithmReadTerminal::Value;
    ComponentTypeId component;
    ComponentFieldSlotId componentField;
    MechanismFieldSlotId targetField;
    AlgorithmReduce reduce = AlgorithmReduce::RequireOne;
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
    // kind == Compare
    CompiledAlgorithmReadPath left;
    CompiledAlgorithmReadPath right;
    AlgorithmCompareOperator compare = AlgorithmCompareOperator::Equal;
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
    // opcode == SetFieldComputed / AddFieldComputed. `hasRight` distinguishes
    // a single read from a binary operation, so "copy this value" does not
    // have to be spelled as "add zero".
    CompiledAlgorithmReadPath left;
    CompiledAlgorithmReadPath right;
    bool hasRight = false;
    AlgorithmBinaryOperator binaryOperator = AlgorithmBinaryOperator::Add;
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
