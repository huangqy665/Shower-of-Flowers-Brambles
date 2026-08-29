#include "runtime_compiler.hpp"

#include <algorithm>
#include <limits>
#include <set>
#include <utility>

namespace dillen::kernel {

namespace {

void AddIssue(
    RuntimeCompileReport& report,
    RuntimeCompileIssueCode code,
    std::string subject,
    std::string message
)
{
    report.issues.push_back({
        code,
        std::move(subject),
        std::move(message)
    });
}

}

bool RuntimeCompileReport::Success() const noexcept
{
    return integrity.Success() && issues.empty();
}

bool RuntimeCompiler::Compile(
    const RulesetDefinition& ruleset,
    const PackageLock& packageLock,
    const MechanismSchemaRegistry& schemas,
    const ComponentSchemaRegistry& componentSchemas,
    const AlgorithmRegistry& algorithms,
    const MechanismDefinitionRegistry& definitions,
    const EntityDefinitionRegistry& entityDefinitions,
    const MechanismSpawnDefinitionRegistry& mechanismSpawns,
    const RuntimeCapabilityContractRegistry& capabilityContracts,
    FrozenRuntimeCatalog& output,
    RuntimeCompileReport& report
) const
{
    RelationSchemaRegistry relationSchemas;
    relationSchemas.Freeze();
    RelationDefinitionRegistry relationDefinitions;
    relationDefinitions.Freeze();
    return Compile(
        ruleset,
        packageLock,
        schemas,
        componentSchemas,
        relationSchemas,
        algorithms,
        definitions,
        entityDefinitions,
        relationDefinitions,
        mechanismSpawns,
        capabilityContracts,
        output,
        report
    );
}

bool RuntimeCompiler::Compile(
    const RulesetDefinition& ruleset,
    const PackageLock& packageLock,
    const MechanismSchemaRegistry& schemas,
    const ComponentSchemaRegistry& componentSchemas,
    const RelationSchemaRegistry& relationSchemas,
    const AlgorithmRegistry& algorithms,
    const MechanismDefinitionRegistry& definitions,
    const EntityDefinitionRegistry& entityDefinitions,
    const RelationDefinitionRegistry& relationDefinitions,
    const MechanismSpawnDefinitionRegistry& mechanismSpawns,
    const RuntimeCapabilityContractRegistry& capabilityContracts,
    FrozenRuntimeCatalog& output,
    RuntimeCompileReport& report
) const
{
    SourceLock sourceLock;
    std::string message;
    SourceLockBuilder{}.Build({}, sourceLock, message);
    return Compile(
        ruleset,
        packageLock,
        sourceLock,
        schemas,
        componentSchemas,
        relationSchemas,
        algorithms,
        definitions,
        entityDefinitions,
        relationDefinitions,
        mechanismSpawns,
        capabilityContracts,
        output,
        report
    );
}

bool RuntimeCompiler::Compile(
    const RulesetDefinition& ruleset,
    const PackageLock& packageLock,
    const SourceLock& sourceLock,
    const MechanismSchemaRegistry& schemas,
    const ComponentSchemaRegistry& componentSchemas,
    const RelationSchemaRegistry& relationSchemas,
    const AlgorithmRegistry& algorithms,
    const MechanismDefinitionRegistry& definitions,
    const EntityDefinitionRegistry& entityDefinitions,
    const RelationDefinitionRegistry& relationDefinitions,
    const MechanismSpawnDefinitionRegistry& mechanismSpawns,
    const RuntimeCapabilityContractRegistry& capabilityContracts,
    FrozenRuntimeCatalog& output,
    RuntimeCompileReport& report
) const
{
    output = {};
    report = {};
    if (!sourceLock.IsResolved())
    {
        AddIssue(
            report,
            RuntimeCompileIssueCode::IntegrityValidationFailed,
            ruleset.canonicalName,
            "Source Lock must be resolved before Runtime compilation"
        );
        return false;
    }
    RulesetIntegrityValidator validator;
    if (!validator.Validate(
            ruleset,
            packageLock,
            schemas,
            componentSchemas,
            relationSchemas,
            algorithms,
            definitions,
            entityDefinitions,
            relationDefinitions,
            mechanismSpawns,
            capabilityContracts,
            report.integrity))
    {
        AddIssue(
            report,
            RuntimeCompileIssueCode::IntegrityValidationFailed,
            ruleset.canonicalName,
            "Ruleset integrity validation failed"
        );
        return false;
    }

    FrozenRuntimeCatalog candidate;
    candidate.ruleset_ = ruleset.id;
    candidate.rulesetVersion_ = ruleset.version;
    candidate.rulesetExtensions_ = ruleset.appliedExtensions;
    candidate.packageLock_ = packageLock;
    candidate.sourceLock_ = sourceLock;
    candidate.fingerprint_ = ComputeRulesetFingerprint(
        ruleset,
        packageLock,
        sourceLock
    );

    candidate.layouts_.reserve(schemas.Size());
    for (const MechanismSchema& schema : schemas.All())
    {
        if (schema.fields.size()
                > std::numeric_limits<std::uint32_t>::max()
            || schema.roles.size()
                > std::numeric_limits<std::uint32_t>::max())
        {
            AddIssue(
                report,
                RuntimeCompileIssueCode::SlotCapacityExceeded,
                schema.canonicalName,
                "Schema exceeds 32-bit Runtime Slot capacity"
            );
            return false;
        }

        CompiledMechanismLayout layout;
        layout.type = schema.type;
        layout.schemaVersion = schema.version;
        layout.fields = schema.fields;
        layout.roles = schema.roles;
        std::sort(
            layout.fields.begin(),
            layout.fields.end(),
            [](const MechanismFieldSchema& first,
               const MechanismFieldSchema& second)
            {
                return first.name < second.name;
            }
        );
        std::sort(
            layout.roles.begin(),
            layout.roles.end(),
            [](const MechanismRoleSchema& first,
               const MechanismRoleSchema& second)
            {
                return first.name < second.name;
            }
        );
        for (std::size_t index = 0; index < layout.fields.size(); ++index)
        {
            layout.fieldSlotsByName.emplace(
                layout.fields[index].name,
                MechanismFieldSlotId{static_cast<std::uint32_t>(index)}
            );
        }
        for (std::size_t index = 0; index < layout.roles.size(); ++index)
        {
            layout.roleSlotsByName.emplace(
                layout.roles[index].name,
                MechanismRoleSlotId{static_cast<std::uint32_t>(index)}
            );
        }
        candidate.layouts_.push_back(std::move(layout));
    }
    std::sort(
        candidate.layouts_.begin(),
        candidate.layouts_.end(),
        [](const CompiledMechanismLayout& first,
           const CompiledMechanismLayout& second)
        {
            if (first.type != second.type)
            {
                return first.type < second.type;
            }
            return first.schemaVersion < second.schemaVersion;
        }
    );
    candidate.RebuildIndexes();

    candidate.relationLayouts_.reserve(relationSchemas.Size());
    for (const RelationSchema& schema : relationSchemas.All())
    {
        candidate.relationLayouts_.push_back({
            schema.type,
            schema.version,
            schema.sourceType,
            schema.targetType,
            schema.allowSelf
        });
    }
    std::sort(
        candidate.relationLayouts_.begin(),
        candidate.relationLayouts_.end(),
        [](const CompiledRelationLayout& first,
           const CompiledRelationLayout& second)
        {
            return first.type != second.type
                ? first.type < second.type
                : first.schemaVersion < second.schemaVersion;
        }
    );
    candidate.RebuildIndexes();

    candidate.componentLayouts_.reserve(componentSchemas.Size());
    for (const ComponentSchema& schema : componentSchemas.All())
    {
        if (schema.fields.size()
            > std::numeric_limits<std::uint32_t>::max())
        {
            AddIssue(
                report,
                RuntimeCompileIssueCode::SlotCapacityExceeded,
                schema.canonicalName,
                "Component Schema exceeds 32-bit Runtime Slot capacity"
            );
            return false;
        }
        CompiledComponentLayout layout;
        layout.type = schema.type;
        layout.schemaVersion = schema.version;
        layout.fields = schema.fields;
        std::sort(
            layout.fields.begin(),
            layout.fields.end(),
            [](const MechanismFieldSchema& first,
               const MechanismFieldSchema& second)
            {
                return first.name < second.name;
            }
        );
        for (std::size_t index = 0; index < layout.fields.size(); ++index)
        {
            layout.fieldSlotsByName.emplace(
                layout.fields[index].name,
                ComponentFieldSlotId{static_cast<std::uint32_t>(index)}
            );
        }
        candidate.componentLayouts_.push_back(std::move(layout));
    }
    std::sort(
        candidate.componentLayouts_.begin(),
        candidate.componentLayouts_.end(),
        [](const CompiledComponentLayout& first,
           const CompiledComponentLayout& second)
        {
            if (first.type != second.type)
            {
                return first.type < second.type;
            }
            return first.schemaVersion < second.schemaVersion;
        }
    );
    candidate.RebuildIndexes();

    candidate.definitions_.reserve(definitions.Size());
    for (const MechanismDefinition& definition : definitions.All())
    {
        const CompiledMechanismLayout* layout = candidate.FindLayout(
            definition.type,
            definition.schemaVersion
        );
        if (layout == nullptr)
        {
            AddIssue(
                report,
                RuntimeCompileIssueCode::DefinitionLayoutMissing,
                definition.canonicalName,
                "Definition has no compiled Runtime Layout"
            );
            return false;
        }

        CompiledMechanismDefinition compiled;
        compiled.id = definition.id;
        compiled.type = definition.type;
        compiled.schemaVersion = definition.schemaVersion;
        compiled.algorithm = definition.algorithm;
        compiled.algorithmVersion = definition.algorithmVersion;
        compiled.initialValues.resize(layout->fields.size());
        compiled.initialRoles.resize(layout->roles.size());
        for (const auto& field : definition.fields)
        {
            const auto slot = layout->fieldSlotsByName.find(field.first);
            if (slot == layout->fieldSlotsByName.end())
            {
                AddIssue(
                    report,
                    RuntimeCompileIssueCode::DefinitionFieldMissing,
                    definition.canonicalName,
                    "Definition field has no compiled Slot"
                );
                return false;
            }
            compiled.initialValues[slot->second.value] = field.second;
        }
        for (const auto& role : definition.roles)
        {
            const auto slot = layout->roleSlotsByName.find(role.first);
            if (slot == layout->roleSlotsByName.end())
            {
                AddIssue(
                    report,
                    RuntimeCompileIssueCode::DefinitionRoleMissing,
                    definition.canonicalName,
                    "Definition role has no compiled Slot"
                );
                return false;
            }
            compiled.initialRoles[slot->second.value] = role.second;
        }
        candidate.definitions_.push_back(std::move(compiled));
    }
    std::sort(
        candidate.definitions_.begin(),
        candidate.definitions_.end(),
        [](const CompiledMechanismDefinition& first,
           const CompiledMechanismDefinition& second)
        {
            return first.id < second.id;
        }
    );

    candidate.entityDefinitions_.reserve(entityDefinitions.Size());
    for (const EntityDefinition& definition : entityDefinitions.All())
    {
        CompiledEntityDefinition compiled;
        compiled.id = definition.id;
        compiled.type = definition.type;
        compiled.components.reserve(definition.components.size());
        for (const EntityComponentDefinition& component
            : definition.components)
        {
            const CompiledComponentLayout* layout =
                candidate.FindComponentLayout(
                    component.type,
                    component.schemaVersion
                );
            if (layout == nullptr)
            {
                AddIssue(
                    report,
                    RuntimeCompileIssueCode::EntityComponentLayoutMissing,
                    definition.canonicalName,
                    "Entity component has no compiled Runtime Layout"
                );
                return false;
            }
            CompiledEntityComponentDefinition compiledComponent;
            compiledComponent.type = component.type;
            compiledComponent.schemaVersion = component.schemaVersion;
            compiledComponent.initialValues.resize(layout->fields.size());
            for (const auto& field : component.fields)
            {
                const auto slot = layout->fieldSlotsByName.find(field.first);
                if (slot == layout->fieldSlotsByName.end())
                {
                    AddIssue(
                        report,
                        RuntimeCompileIssueCode::EntityComponentFieldMissing,
                        definition.canonicalName,
                        "Entity component field has no compiled Slot"
                    );
                    return false;
                }
                compiledComponent.initialValues[slot->second.value] =
                    field.second;
            }
            compiled.components.push_back(std::move(compiledComponent));
        }
        candidate.entityDefinitions_.push_back(std::move(compiled));
    }
    std::sort(
        candidate.entityDefinitions_.begin(),
        candidate.entityDefinitions_.end(),
        [](const CompiledEntityDefinition& first,
           const CompiledEntityDefinition& second)
        {
            return first.id < second.id;
        }
    );
    candidate.RebuildIndexes();

    candidate.relationDefinitions_.reserve(relationDefinitions.Size());
    for (const RelationDefinition& definition
        : relationDefinitions.All())
    {
        if (candidate.FindRelationLayout(
                definition.type,
                definition.schemaVersion) == nullptr
            || candidate.FindEntityDefinition(definition.source) == nullptr
            || candidate.FindEntityDefinition(definition.target) == nullptr)
        {
            AddIssue(
                report,
                RuntimeCompileIssueCode::DefinitionLayoutMissing,
                definition.canonicalName,
                "Relation Definition has no compiled layout or endpoint"
            );
            return false;
        }
        candidate.relationDefinitions_.push_back({
            definition.id,
            definition.type,
            definition.schemaVersion,
            definition.source,
            definition.target
        });
    }
    std::sort(
        candidate.relationDefinitions_.begin(),
        candidate.relationDefinitions_.end(),
        [](const CompiledRelationDefinition& first,
           const CompiledRelationDefinition& second)
        {
            return first.id < second.id;
        }
    );
    candidate.RebuildIndexes();

    candidate.spawnDefinitions_.reserve(mechanismSpawns.Size());
    for (const MechanismSpawnDefinition& spawn : mechanismSpawns.All())
    {
        const MechanismDefinition* sourceDefinition = definitions.Find(
            spawn.definition
        );
        const CompiledMechanismDefinition* compiledDefinition =
            candidate.FindDefinition(spawn.definition);
        if (sourceDefinition == nullptr || compiledDefinition == nullptr)
        {
            AddIssue(
                report,
                RuntimeCompileIssueCode::SpawnDefinitionMissing,
                spawn.canonicalName,
                "Spawn has no compiled Mechanism Definition"
            );
            return false;
        }
        const CompiledMechanismLayout* layout = candidate.FindLayout(
            sourceDefinition->type,
            sourceDefinition->schemaVersion
        );
        CompiledMechanismSpawnDefinition compiled;
        compiled.id = spawn.id;
        compiled.definition = spawn.definition;
        compiled.count = spawn.count;
        compiled.initialValues.resize(layout->fields.size());
        compiled.initialRoles.resize(layout->roles.size());
        for (const auto& field : spawn.initialFields)
        {
            const auto slot = layout->fieldSlotsByName.find(field.first);
            if (slot == layout->fieldSlotsByName.end())
            {
                AddIssue(
                    report,
                    RuntimeCompileIssueCode::SpawnFieldMissing,
                    spawn.canonicalName,
                    "Spawn field has no compiled Slot"
                );
                return false;
            }
            compiled.initialValues[slot->second.value] = field.second;
        }
        for (const auto& role : spawn.initialRoles)
        {
            const auto slot = layout->roleSlotsByName.find(role.first);
            if (slot == layout->roleSlotsByName.end())
            {
                AddIssue(
                    report,
                    RuntimeCompileIssueCode::SpawnRoleMissing,
                    spawn.canonicalName,
                    "Spawn role has no compiled Slot"
                );
                return false;
            }
            compiled.initialRoles[slot->second.value] = role.second;
        }
        candidate.spawnDefinitions_.push_back(std::move(compiled));
    }
    std::sort(
        candidate.spawnDefinitions_.begin(),
        candidate.spawnDefinitions_.end(),
        [](const CompiledMechanismSpawnDefinition& first,
           const CompiledMechanismSpawnDefinition& second)
        {
            return first.id < second.id;
        }
    );
    std::set<std::pair<std::uint64_t, std::uint32_t>> compiledCapabilities;
    for (const PackageLockEntry& package : packageLock.Entries())
    {
        for (const CapabilityProvision& provision
            : package.providedCapabilities)
        {
            const RuntimeCapabilityContract* contract =
                capabilityContracts.Find(
                    provision.capability,
                    provision.version
                );
            if (contract != nullptr
                && compiledCapabilities.emplace(
                    contract->id.value,
                    contract->version).second)
            {
                candidate.capabilities_.push_back(*contract);
            }
        }
    }
    std::sort(
        candidate.capabilities_.begin(),
        candidate.capabilities_.end(),
        [](const RuntimeCapabilityContract& first,
           const RuntimeCapabilityContract& second)
        {
            if (first.id != second.id)
            {
                return first.id < second.id;
            }
            return first.version < second.version;
        }
    );
    candidate.RebuildIndexes();
    RuntimeCapabilityResolver capabilityResolver;
    candidate.algorithmCapabilityBindings_.reserve(algorithms.Size());
    for (const AlgorithmDescriptor& algorithm : algorithms.All())
    {
        CompiledAlgorithmCapabilityBinding binding;
        binding.algorithm = algorithm.id;
        binding.algorithmVersion = algorithm.version;
        for (const CapabilityRequirement& requirement
            : algorithm.requiredCapabilities)
        {
            ResolvedCapabilityContract resolved;
            if (capabilityResolver.Resolve(
                    requirement,
                    capabilityContracts,
                    packageLock,
                    resolved) != CapabilityResolveResult::Resolved)
            {
                AddIssue(
                    report,
                    RuntimeCompileIssueCode::IntegrityValidationFailed,
                    algorithm.canonicalName,
                    "Algorithm Capability binding could not be resolved"
                );
                return false;
            }
            const auto slot = candidate.capabilityIndex_.find({
                resolved.capability.value,
                resolved.version
            });
            if (slot == candidate.capabilityIndex_.end()
                || slot->second > std::numeric_limits<std::uint32_t>::max())
            {
                AddIssue(
                    report,
                    RuntimeCompileIssueCode::SlotCapacityExceeded,
                    algorithm.canonicalName,
                    "Capability binding exceeds Runtime Slot capacity"
                );
                return false;
            }
            binding.capabilities.push_back(CapabilityBindingSlotId{
                static_cast<std::uint32_t>(slot->second)
            });
        }
        candidate.algorithmCapabilityBindings_.push_back(
            std::move(binding)
        );
    }
    candidate.algorithms_ = algorithms.All();
    candidate.RebuildIndexes();
    for (const CompiledMechanismDefinition& definition
        : candidate.definitions_)
    {
        if (!definition.algorithm)
        {
            continue;
        }
        const AlgorithmDescriptor* algorithm = candidate.FindAlgorithm(
            definition.algorithm,
            definition.algorithmVersion
        );
        if (algorithm == nullptr
            || algorithm->backend != AlgorithmBackend::Declarative)
        {
            continue;
        }
        const CompiledMechanismLayout* layout = candidate.FindLayout(
            definition.type,
            definition.schemaVersion
        );
        if (layout == nullptr
            || !IsValidAlgorithmProgram(
                algorithm->program,
                algorithm->entryPoints))
        {
            AddIssue(
                report,
                RuntimeCompileIssueCode::AlgorithmProgramMissing,
                algorithm->canonicalName,
                "Declarative Algorithm has no valid source program"
            );
            return false;
        }

        CompiledAlgorithmProgram program;
        program.definition = definition.id;
        program.algorithm = definition.algorithm;
        program.algorithmVersion = definition.algorithmVersion;
        for (const auto& sourceStage : algorithm->program.stages)
        {
            if (sourceStage.second.size()
                > algorithm->executionPolicy.instructionBudget)
            {
                AddIssue(
                    report,
                    RuntimeCompileIssueCode::
                        AlgorithmProgramBudgetExceeded,
                    algorithm->canonicalName,
                    "Declarative stage exceeds its instruction budget"
                );
                return false;
            }
            std::vector<AlgorithmBytecodeInstruction> bytecode;
            bytecode.reserve(sourceStage.second.size());
            for (const AlgorithmInstructionDefinition& sourceInstruction
                : sourceStage.second)
            {
                AlgorithmBytecodeInstruction instruction;
                for (const AlgorithmConditionDefinition& sourceCondition
                    : sourceInstruction.conditions)
                {
                    CompiledAlgorithmCondition condition;
                    condition.kind = sourceCondition.kind;
                    condition.value = sourceCondition.value;
                    condition.queryKind = sourceCondition.queryKind;
                    condition.queryType = sourceCondition.queryType;
                    condition.minimumCount = sourceCondition.minimumCount;
                    condition.eventType = sourceCondition.eventType;
                    condition.rngStream = sourceCondition.rngStream;
                    condition.rngOffset = sourceCondition.rngOffset;
                    condition.rngModulo = sourceCondition.rngModulo;
                    condition.rngEquals = sourceCondition.rngEquals;
                    if (sourceCondition.kind
                        == AlgorithmConditionKind::SelfFieldEquals)
                    {
                        const auto field = layout->fieldSlotsByName.find(
                            sourceCondition.field
                        );
                        if (field == layout->fieldSlotsByName.end()
                            || !MechanismValueMatchesSchema(
                                layout->fields[field->second.value],
                                sourceCondition.value))
                        {
                            AddIssue(
                                report,
                                RuntimeCompileIssueCode::
                                    AlgorithmProgramOperandInvalid,
                                algorithm->canonicalName,
                                "declarative condition references an invalid field"
                            );
                            return false;
                        }
                        condition.field = field->second;
                    }
                    instruction.conditions.push_back(std::move(condition));
                }
                if (sourceInstruction.kind
                    == AlgorithmInstructionKind::TransitionLifecycle)
                {
                    instruction.opcode = AlgorithmBytecodeOpcode::
                        TransitionLifecycle;
                    instruction.lifecycle = sourceInstruction.lifecycle;
                    bytecode.push_back(std::move(instruction));
                    continue;
                }

                if (sourceInstruction.kind
                    == AlgorithmInstructionKind::CreateEntity)
                {
                    if (candidate.FindEntityDefinition(
                            sourceInstruction.entityDefinition) == nullptr)
                    {
                        AddIssue(
                            report,
                            RuntimeCompileIssueCode::
                                AlgorithmProgramOperandInvalid,
                            algorithm->canonicalName,
                            "create_entity references an unknown definition"
                        );
                        return false;
                    }
                    instruction.opcode =
                        AlgorithmBytecodeOpcode::CreateEntity;
                    instruction.entityDefinition =
                        sourceInstruction.entityDefinition;
                    bytecode.push_back(std::move(instruction));
                    continue;
                }
                if (sourceInstruction.kind
                    == AlgorithmInstructionKind::SetComponentField)
                {
                    const CompiledEntityDefinition* ownerDefinition = nullptr;
                    for (const CompiledEntityDefinition& entity
                        : candidate.entityDefinitions_)
                    {
                        if (StableEntityId(entity.id)
                            == sourceInstruction.entity)
                        {
                            ownerDefinition = &entity;
                            break;
                        }
                    }
                    const CompiledEntityComponentDefinition* component =
                        nullptr;
                    if (ownerDefinition != nullptr)
                    {
                        for (const CompiledEntityComponentDefinition& entry
                            : ownerDefinition->components)
                        {
                            if (entry.type == sourceInstruction.component)
                            {
                                component = &entry;
                                break;
                            }
                        }
                    }
                    const CompiledComponentLayout* componentLayout =
                        component == nullptr
                        ? nullptr
                        : candidate.FindComponentLayout(
                            component->type,
                            component->schemaVersion
                        );
                    const auto componentField = componentLayout == nullptr
                        ? std::map<std::string, ComponentFieldSlotId>::
                            const_iterator{}
                        : componentLayout->fieldSlotsByName.find(
                            sourceInstruction.componentField
                        );
                    if (componentLayout == nullptr
                        || componentField
                            == componentLayout->fieldSlotsByName.end()
                        || !MechanismValueMatchesSchema(
                            componentLayout->fields[
                                componentField->second.value],
                            sourceInstruction.operand))
                    {
                        AddIssue(
                            report,
                            RuntimeCompileIssueCode::
                                AlgorithmProgramOperandInvalid,
                            algorithm->canonicalName,
                            "set_component_field references an invalid target"
                        );
                        return false;
                    }
                    instruction.opcode = AlgorithmBytecodeOpcode::
                        SetComponentFieldConstant;
                    instruction.entity = sourceInstruction.entity;
                    instruction.component = sourceInstruction.component;
                    instruction.componentField = componentField->second;
                    instruction.operand = sourceInstruction.operand;
                    bytecode.push_back(std::move(instruction));
                    continue;
                }
                if (sourceInstruction.kind
                    == AlgorithmInstructionKind::AddRelation)
                {
                    const bool relationKnown = std::any_of(
                        candidate.relationLayouts_.begin(),
                        candidate.relationLayouts_.end(),
                        [&sourceInstruction](
                            const CompiledRelationLayout& relation)
                        {
                            return relation.type
                                == sourceInstruction.relationType;
                        }
                    );
                    if (!relationKnown)
                    {
                        AddIssue(
                            report,
                            RuntimeCompileIssueCode::
                                AlgorithmProgramOperandInvalid,
                            algorithm->canonicalName,
                            "add_relation references an unknown schema"
                        );
                        return false;
                    }
                    instruction.opcode =
                        AlgorithmBytecodeOpcode::AddRelation;
                    instruction.relationType = sourceInstruction.relationType;
                    instruction.sourceEntity = sourceInstruction.sourceEntity;
                    instruction.targetEntity = sourceInstruction.targetEntity;
                    bytecode.push_back(std::move(instruction));
                    continue;
                }
                if (sourceInstruction.kind
                    == AlgorithmInstructionKind::RemoveRelation)
                {
                    instruction.opcode =
                        AlgorithmBytecodeOpcode::RemoveRelation;
                    instruction.relation = sourceInstruction.relation;
                    bytecode.push_back(std::move(instruction));
                    continue;
                }
                if (sourceInstruction.kind
                    == AlgorithmInstructionKind::SpawnMechanism)
                {
                    if (candidate.FindSpawnDefinition(
                            sourceInstruction.spawn) == nullptr)
                    {
                        AddIssue(
                            report,
                            RuntimeCompileIssueCode::
                                AlgorithmProgramOperandInvalid,
                            algorithm->canonicalName,
                            "spawn_mechanism references an unknown spawn"
                        );
                        return false;
                    }
                    instruction.opcode =
                        AlgorithmBytecodeOpcode::SpawnMechanism;
                    instruction.spawn = sourceInstruction.spawn;
                    bytecode.push_back(std::move(instruction));
                    continue;
                }
                if (sourceInstruction.kind
                    == AlgorithmInstructionKind::ScheduleEvent)
                {
                    instruction.opcode =
                        AlgorithmBytecodeOpcode::ScheduleEvent;
                    instruction.eventType = sourceInstruction.eventType;
                    instruction.dueTickOffset =
                        sourceInstruction.dueTickOffset;
                    instruction.priority = sourceInstruction.priority;
                    instruction.payload = sourceInstruction.payload;
                    bytecode.push_back(std::move(instruction));
                    continue;
                }
                if (sourceInstruction.kind
                    == AlgorithmInstructionKind::CancelEvent)
                {
                    instruction.opcode =
                        AlgorithmBytecodeOpcode::CancelEvent;
                    instruction.eventSequence =
                        sourceInstruction.eventSequence;
                    bytecode.push_back(std::move(instruction));
                    continue;
                }
                if (sourceInstruction.kind
                    == AlgorithmInstructionKind::CreateRngStream)
                {
                    instruction.opcode =
                        AlgorithmBytecodeOpcode::CreateRngStream;
                    instruction.rngStream = sourceInstruction.rngStream;
                    instruction.rngSeed = sourceInstruction.rngSeed;
                    bytecode.push_back(std::move(instruction));
                    continue;
                }
                if (sourceInstruction.kind
                    == AlgorithmInstructionKind::AdvanceRngStream)
                {
                    instruction.opcode =
                        AlgorithmBytecodeOpcode::AdvanceRngStream;
                    instruction.rngStream = sourceInstruction.rngStream;
                    instruction.rngCount = sourceInstruction.rngCount;
                    bytecode.push_back(std::move(instruction));
                    continue;
                }

                const auto field = layout->fieldSlotsByName.find(
                    sourceInstruction.field
                );
                if (field == layout->fieldSlotsByName.end())
                {
                    AddIssue(
                        report,
                        RuntimeCompileIssueCode::AlgorithmProgramFieldMissing,
                        algorithm->canonicalName,
                        "Algorithm field has no compiled Runtime Slot: "
                            + sourceInstruction.field
                    );
                    return false;
                }
                instruction.field = field->second;
                instruction.operand = sourceInstruction.operand;
                const MechanismFieldSchema& fieldSchema =
                    layout->fields[field->second.value];
                if (sourceInstruction.kind
                    == AlgorithmInstructionKind::SetField)
                {
                    if (!MechanismValueMatchesSchema(
                            fieldSchema,
                            instruction.operand))
                    {
                        AddIssue(
                            report,
                            RuntimeCompileIssueCode::
                                AlgorithmProgramOperandInvalid,
                            algorithm->canonicalName,
                            "set_field value violates the target field schema"
                        );
                        return false;
                    }
                    instruction.opcode =
                        AlgorithmBytecodeOpcode::SetFieldConstant;
                }
                else if (fieldSchema.kind == MechanismValueKind::Integer
                    && instruction.operand.Kind()
                        == MechanismValueKind::Integer)
                {
                    instruction.opcode =
                        AlgorithmBytecodeOpcode::AddIntegerConstant;
                }
                else if (fieldSchema.kind == MechanismValueKind::Decimal
                    && (instruction.operand.Kind()
                            == MechanismValueKind::Decimal
                        || instruction.operand.Kind()
                            == MechanismValueKind::Integer))
                {
                    if (instruction.operand.Kind()
                        == MechanismValueKind::Integer)
                    {
                        instruction.operand = MechanismValue(
                            static_cast<double>(std::get<std::int64_t>(
                                instruction.operand.data
                            ))
                        );
                    }
                    instruction.opcode =
                        AlgorithmBytecodeOpcode::AddDecimalConstant;
                }
                else
                {
                    AddIssue(
                        report,
                        RuntimeCompileIssueCode::
                            AlgorithmProgramOperandInvalid,
                        algorithm->canonicalName,
                        "add_field requires a compatible numeric field"
                    );
                    return false;
                }
                bytecode.push_back(std::move(instruction));
            }
            program.stages.emplace(
                sourceStage.first,
                std::move(bytecode)
            );
        }
        candidate.algorithmPrograms_.push_back(std::move(program));
    }
    std::sort(
        candidate.algorithmPrograms_.begin(),
        candidate.algorithmPrograms_.end(),
        [](const CompiledAlgorithmProgram& first,
           const CompiledAlgorithmProgram& second)
        {
            return first.definition < second.definition;
        }
    );
    candidate.RebuildIndexes();
    candidate.frozen_ = true;
    output = std::move(candidate);
    return true;
}

}
