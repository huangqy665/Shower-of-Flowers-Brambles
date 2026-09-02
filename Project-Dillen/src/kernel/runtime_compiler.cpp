#include "runtime_compiler.hpp"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace dillen::kernel {

namespace {

struct RuntimeCompileSelection
{
    std::set<std::pair<MechanismTypeId, std::uint32_t>> schemas;
    std::set<std::pair<ComponentTypeId, std::uint32_t>> components;
    std::set<std::pair<RelationTypeId, std::uint32_t>> relations;
    std::set<MechanismDefinitionId> definitions;
    std::set<EntityDefinitionId> entityDefinitions;
    std::set<RelationDefinitionId> relationDefinitions;
    std::set<MechanismSpawnDefinitionId> spawns;
    std::set<std::pair<AlgorithmId, std::uint32_t>> algorithms;
    std::set<std::pair<CapabilityId, std::uint32_t>> capabilities;
};

// Picks the highest contract version of `capability` that falls inside `range`
// AND is declared as provided by a package in the resolved Package Lock.
// Returns nullptr otherwise. Scoping to the Package Lock (not the whole
// registry) keeps the Kernel compile self-sealing: an `invoke_capability` or
// `provides_capabilities` can only bind to a contract version the locked
// package set actually owns. Mirrors RuntimeCapabilityResolver::Resolve.
const RuntimeCapabilityContract* ResolveCapabilityVersion(
    const RuntimeCapabilityContractRegistry& contracts,
    const PackageLock& packageLock,
    CapabilityId capability,
    const CapabilityVersionRange& range
)
{
    const RuntimeCapabilityContract* best = nullptr;
    for (const PackageLockEntry& package : packageLock.Entries())
    {
        for (const CapabilityProvision& provision
            : package.providedCapabilities)
        {
            if (provision.capability != capability
                || !range.Contains(provision.version))
            {
                continue;
            }
            const RuntimeCapabilityContract* contract = contracts.Find(
                provision.capability,
                provision.version
            );
            if (contract != nullptr
                && (best == nullptr || contract->version > best->version))
            {
                best = contract;
            }
        }
    }
    return best;
}

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

// Lowers one authored read path to slots.
//
// Every name becomes a slot here, at load time, so the run-time evaluator never
// does a string lookup. The shape rules are enforced here too rather than in
// the VM, because "this role is bound to Entities but the path reads a
// Mechanism field" is an authoring mistake that should stop the build, not
// fault an instance halfway through a campaign.
//
// The data model dictates one ordering that is worth stating plainly: a
// Mechanism Instance has fields and role slots but no Components, so a path
// that wants a Component field must go through a role slot bound to an Entity,
// and a Relation hop must start from such an Entity. There is no way to
// shortcut that, and no reason to want one.
bool LowerReadPath(
    const AlgorithmReadPathDefinition& source,
    const CompiledMechanismLayout& layout,
    const std::vector<CompiledMechanismLayout>& mechanismLayouts,
    const std::vector<CompiledComponentLayout>& componentLayouts,
    const std::vector<CompiledRelationLayout>& relationLayouts,
    const std::string& algorithmName,
    RuntimeCompileReport& report,
    CompiledAlgorithmReadPath& out
)
{
    const auto reject = [&](const std::string& message)
    {
        AddIssue(
            report,
            RuntimeCompileIssueCode::AlgorithmProgramOperandInvalid,
            algorithmName,
            message
        );
        return false;
    };

    out = CompiledAlgorithmReadPath{};
    out.root = source.root;
    out.reduce = source.reduce;
    out.terminal = source.terminal;
    out.traverseRelation = source.traverseRelation;
    out.relationType = source.relationType;
    out.direction = source.direction;
    out.component = source.component;

    switch (source.root)
    {
    case AlgorithmReadRoot::Constant:
        out.constant = source.constant;
        if (out.constant.Kind() != MechanismValueKind::Integer
            && out.constant.Kind() != MechanismValueKind::Decimal)
        {
            return reject("a read path constant must be numeric");
        }
        return true;
    case AlgorithmReadRoot::EventPayload:
        return true;
    case AlgorithmReadRoot::SelfField:
    {
        const auto field = layout.fieldSlotsByName.find(source.selfField);
        if (field == layout.fieldSlotsByName.end())
        {
            return reject(
                "read path names an unknown field: " + source.selfField);
        }
        const MechanismValueKind kind = layout.fields[field->second.value].kind;
        if (kind != MechanismValueKind::Integer
            && kind != MechanismValueKind::Decimal)
        {
            return reject(
                "read path field '" + source.selfField + "' is not numeric");
        }
        out.selfField = field->second;
        return true;
    }
    case AlgorithmReadRoot::RoleTarget:
        break;
    }

    const auto role = layout.roleSlotsByName.find(source.role);
    if (role == layout.roleSlotsByName.end())
    {
        return reject("read path names an unknown role: " + source.role);
    }
    out.role = role->second;
    const MechanismRoleSchema& roleSchema = layout.roles[role->second.value];

    if (source.terminal == AlgorithmReadTerminal::MechanismField)
    {
        if (source.traverseRelation)
        {
            return reject(
                "a Relation hop ends at an Entity, so it cannot read a "
                "Mechanism field");
        }
        if (roleSchema.referenceKind
            != MechanismReferenceKind::MechanismInstance)
        {
            return reject(
                "role '" + source.role
                    + "' does not reference Mechanism Instances");
        }
        // Resolve against the role's DECLARED target type, not the calling
        // Mechanism's own layout. Without `reference_type` the compiler cannot
        // know which slot space `target_field` lives in; it used to fall back
        // on the caller's layout, which silently resolves to a same-named
        // field of an unrelated type and reads the wrong slot at run time.
        // That is a wrong answer rather than an error, so it is rejected now.
        if (!roleSchema.referenceType)
        {
            return reject(
                "role '" + source.role + "' must declare reference_type "
                "before a read path can name a field on its target");
        }
        const MechanismTypeId targetType{*roleSchema.referenceType};
        const CompiledMechanismLayout* targetLayout = nullptr;
        for (const CompiledMechanismLayout& candidateLayout : mechanismLayouts)
        {
            if (candidateLayout.type == targetType)
            {
                targetLayout = &candidateLayout;
                break;
            }
        }
        if (targetLayout == nullptr)
        {
            return reject(
                "role '" + source.role + "' declares a reference_type that is "
                "not a Mechanism Type in this Ruleset");
        }
        const auto field =
            targetLayout->fieldSlotsByName.find(source.targetField);
        if (field == targetLayout->fieldSlotsByName.end())
        {
            return reject(
                "read path names an unknown target field: "
                    + source.targetField);
        }
        const MechanismValueKind targetKind =
            targetLayout->fields[field->second.value].kind;
        if (targetKind != MechanismValueKind::Integer
            && targetKind != MechanismValueKind::Decimal)
        {
            return reject(
                "read path target field '" + source.targetField
                    + "' is not numeric");
        }
        out.targetField = field->second;
        return true;
    }

    if (source.terminal != AlgorithmReadTerminal::ComponentField)
    {
        return reject("a role read path must name a terminal field");
    }
    if (roleSchema.referenceKind != MechanismReferenceKind::Entity)
    {
        return reject(
            "role '" + source.role + "' does not reference Entities, so it "
            "cannot reach a Component");
    }
    if (source.traverseRelation)
    {
        bool relationKnown = false;
        for (const CompiledRelationLayout& relation
            : relationLayouts)
        {
            if (relation.type == source.relationType)
            {
                relationKnown = true;
                break;
            }
        }
        if (!relationKnown)
        {
            return reject("read path traverses an unknown Relation type");
        }
    }

    const CompiledComponentLayout* componentLayout = nullptr;
    for (const CompiledComponentLayout& compiled : componentLayouts)
    {
        if (compiled.type == source.component)
        {
            componentLayout = &compiled;
            break;
        }
    }
    if (componentLayout == nullptr)
    {
        return reject("read path names an unknown Component type");
    }
    const auto componentField =
        componentLayout->fieldSlotsByName.find(source.componentField);
    if (componentField == componentLayout->fieldSlotsByName.end())
    {
        return reject(
            "read path names an unknown Component field: "
                + source.componentField);
    }
    const MechanismValueKind kind =
        componentLayout->fields[componentField->second.value].kind;
    if (kind != MechanismValueKind::Integer
        && kind != MechanismValueKind::Decimal)
    {
        return reject(
            "read path Component field '" + source.componentField
                + "' is not numeric");
    }
    out.componentField = componentField->second;
    return true;
}

// A Capability Contract identity may be provided by at most ONE package in a
// composed Ruleset. Two packages both declaring `provides` for the same
// contract is a conflict, not a choice: nothing in the Kernel can say which one
// a given Definition or Algorithm meant, because MechanismDefinitionSource
// carries only a source-layer name and AlgorithmDescriptor carries no origin at
// all. Rejecting the ambiguity keeps version selection well defined without
// inventing an ownership chain. (Deliberately strict before the Demo 0.2
// contract freeze -- a later multi-provider model can relax this; it could not
// tighten it.)
bool RejectAmbiguousCapabilityProviders(
    const PackageLock& packageLock,
    RuntimeCompileReport& report
)
{
    std::map<std::uint64_t, const PackageLockEntry*> owners;
    for (const PackageLockEntry& package : packageLock.Entries())
    {
        for (const CapabilityProvision& provision
            : package.providedCapabilities)
        {
            const auto existing = owners.emplace(
                provision.capability.value,
                &package
            );
            if (!existing.second
                && existing.first->second->package != package.package)
            {
                AddIssue(
                    report,
                    RuntimeCompileIssueCode::IntegrityValidationFailed,
                    provision.canonicalName,
                    "Capability Contract is provided by more than one locked "
                    "Package ('" + existing.first->second->canonicalName
                        + "' and '" + package.canonicalName
                        + "'); a contract identity must have a single owner"
                );
                return false;
            }
        }
    }
    return true;
}

bool BuildCompileSelection(
    const RulesetDefinition& ruleset,
    const PackageLock& packageLock,
    const AlgorithmRegistry& algorithms,
    const MechanismDefinitionRegistry& definitions,
    const EntityDefinitionRegistry& entityDefinitions,
    const RelationDefinitionRegistry& relationDefinitions,
    const MechanismSpawnDefinitionRegistry& mechanismSpawns,
    const RuntimeCapabilityContractRegistry& capabilityContracts,
    RuntimeCompileSelection& output,
    RuntimeCompileReport& report
)
{
    for (const RulesetSchemaRequirement& requirement
        : ruleset.requiredSchemas)
    {
        output.schemas.emplace(requirement.type, requirement.version);
    }
    for (const RulesetComponentRequirement& requirement
        : ruleset.requiredComponents)
    {
        output.components.emplace(requirement.type, requirement.version);
    }
    for (const RulesetRelationRequirement& requirement
        : ruleset.requiredRelations)
    {
        output.relations.emplace(requirement.type, requirement.version);
    }
    output.definitions.insert(
        ruleset.requiredDefinitions.begin(),
        ruleset.requiredDefinitions.end()
    );
    output.entityDefinitions.insert(
        ruleset.requiredEntityDefinitions.begin(),
        ruleset.requiredEntityDefinitions.end()
    );
    output.relationDefinitions.insert(
        ruleset.requiredRelationDefinitions.begin(),
        ruleset.requiredRelationDefinitions.end()
    );
    // The wholesale forms. Bounded by the registries, which hold exactly what
    // the locked Packages declared.
    if (ruleset.requireAllEntityDefinitions)
    {
        for (const EntityDefinition& entity : entityDefinitions.All())
        {
            output.entityDefinitions.insert(entity.id);
        }
    }
    if (ruleset.requireAllRelationDefinitions)
    {
        for (const RelationDefinition& relation : relationDefinitions.All())
        {
            output.relationDefinitions.insert(relation.id);
        }
    }
    if (ruleset.requireAllMechanismSpawns)
    {
        for (const MechanismSpawnDefinition& spawn : mechanismSpawns.All())
        {
            output.spawns.insert(spawn.id);
        }
    }
    output.spawns.insert(
        ruleset.requiredMechanismSpawns.begin(),
        ruleset.requiredMechanismSpawns.end()
    );
    for (const RulesetAlgorithmRequirement& requirement
        : ruleset.requiredAlgorithms)
    {
        output.algorithms.emplace(
            requirement.algorithm,
            requirement.version
        );
    }

    bool changed = true;
    while (changed)
    {
        changed = false;
        for (MechanismSpawnDefinitionId id
            : std::vector<MechanismSpawnDefinitionId>(
                output.spawns.begin(), output.spawns.end()))
        {
            const MechanismSpawnDefinition* spawn = mechanismSpawns.Find(id);
            if (spawn != nullptr)
            {
                changed = output.definitions.emplace(
                    spawn->definition).second || changed;
            }
        }
        for (MechanismDefinitionId id
            : std::vector<MechanismDefinitionId>(
                output.definitions.begin(), output.definitions.end()))
        {
            const MechanismDefinition* definition = definitions.Find(id);
            if (definition == nullptr)
            {
                continue;
            }
            changed = output.schemas.emplace(
                definition->type,
                definition->schemaVersion).second || changed;
            if (definition->algorithm)
            {
                changed = output.algorithms.emplace(
                    definition->algorithm,
                    definition->algorithmVersion).second || changed;
            }
            for (const CapabilityProvisionDeclaration& declaration
                : definition->providedCapabilities)
            {
                const RuntimeCapabilityContract* contract =
                    ResolveCapabilityVersion(
                        capabilityContracts,
                        packageLock,
                        StableCapabilityId(declaration.capabilityName),
                        declaration.versions
                    );
                if (contract != nullptr)
                {
                    changed = output.capabilities.emplace(
                        contract->id,
                        contract->version).second || changed;
                }
            }
        }
        for (RelationDefinitionId id
            : std::vector<RelationDefinitionId>(
                output.relationDefinitions.begin(),
                output.relationDefinitions.end()))
        {
            const RelationDefinition* definition =
                relationDefinitions.Find(id);
            if (definition == nullptr)
            {
                continue;
            }
            changed = output.relations.emplace(
                definition->type,
                definition->schemaVersion).second || changed;
            changed = output.entityDefinitions.emplace(
                definition->source).second || changed;
            changed = output.entityDefinitions.emplace(
                definition->target).second || changed;
        }
        for (EntityDefinitionId id
            : std::vector<EntityDefinitionId>(
                output.entityDefinitions.begin(),
                output.entityDefinitions.end()))
        {
            const EntityDefinition* definition = entityDefinitions.Find(id);
            if (definition == nullptr)
            {
                continue;
            }
            for (const EntityComponentDefinition& component
                : definition->components)
            {
                changed = output.components.emplace(
                    component.type,
                    component.schemaVersion).second || changed;
            }
        }
        for (const auto& selectedAlgorithm
            : std::vector<std::pair<AlgorithmId, std::uint32_t>>(
                output.algorithms.begin(), output.algorithms.end()))
        {
            const AlgorithmDescriptor* algorithm = algorithms.Find(
                selectedAlgorithm.first,
                selectedAlgorithm.second
            );
            if (algorithm == nullptr)
            {
                continue;
            }
            const auto closeOverInstruction =
                [&](const AlgorithmInstructionDefinition& instruction) -> bool
            {
                if (instruction.kind
                    == AlgorithmInstructionKind::CreateEntity)
                {
                        if (entityDefinitions.Find(
                                instruction.entityDefinition) == nullptr)
                        {
                            AddIssue(
                                report,
                                RuntimeCompileIssueCode::
                                    AlgorithmProgramOperandInvalid,
                                algorithm->canonicalName,
                                "selected create_entity target is missing"
                            );
                            return false;
                        }
                        changed = output.entityDefinitions.emplace(
                            instruction.entityDefinition).second || changed;
                    }
                    else if (instruction.kind
                        == AlgorithmInstructionKind::SetComponentField
                        || instruction.kind
                            == AlgorithmInstructionKind::
                                SetComponentFieldComputed)
                    {
                        bool found = false;
                        for (const EntityDefinition& entity
                            : entityDefinitions.All())
                        {
                            if (StableEntityId(entity.id)
                                == instruction.entity)
                            {
                                changed = output.entityDefinitions.emplace(
                                    entity.id).second || changed;
                                found = true;
                                break;
                            }
                        }
                        if (!found)
                        {
                            AddIssue(
                                report,
                                RuntimeCompileIssueCode::
                                    AlgorithmProgramOperandInvalid,
                                algorithm->canonicalName,
                                "selected set_component_field target is missing"
                            );
                            return false;
                        }
                    }
                    else if (instruction.kind
                        == AlgorithmInstructionKind::SpawnMechanism)
                    {
                        if (mechanismSpawns.Find(
                                instruction.spawn) == nullptr)
                        {
                            AddIssue(
                                report,
                                RuntimeCompileIssueCode::
                                    AlgorithmProgramOperandInvalid,
                                algorithm->canonicalName,
                                "selected spawn_mechanism target is missing"
                            );
                            return false;
                        }
                        changed = output.spawns.emplace(
                            instruction.spawn).second || changed;
                    }
                    else if (instruction.kind
                        == AlgorithmInstructionKind::InvokeCapability)
                    {
                        const RuntimeCapabilityContract* contract =
                            ResolveCapabilityVersion(
                                capabilityContracts,
                                packageLock,
                                StableCapabilityId(
                                    instruction.capabilityName
                                ),
                                instruction.capabilityVersions
                            );
                        if (contract == nullptr)
                        {
                            AddIssue(
                                report,
                                RuntimeCompileIssueCode::
                                    AlgorithmProgramOperandInvalid,
                                algorithm->canonicalName,
                                "invoke_capability names a Capability Contract "
                                "with no version compatible with the request"
                            );
                            return false;
                        }
                        changed = output.capabilities.emplace(
                            contract->id,
                            contract->version).second || changed;
                    }
                return true;
            };
            for (const auto& stage : algorithm->program.stages)
            {
                for (const AlgorithmInstructionDefinition& instruction
                    : stage.second)
                {
                    if (!closeOverInstruction(instruction))
                    {
                        return false;
                    }
                }
            }
            // Script backend: Transact instructions carry a declarative action
            // whose Entity / Spawn / Capability references must enter the
            // compile closure exactly like a declarative instruction's.
            for (const auto& scriptStage : algorithm->script.stages)
            {
                for (const ControlledScriptInstructionDefinition& source
                    : scriptStage.second)
                {
                    if (source.kind
                            == ControlledScriptInstructionKind::Transact
                        && !closeOverInstruction(source.action))
                    {
                        return false;
                    }
                }
            }
        }
    }

    RuntimeCapabilityResolver capabilityResolver;
    std::vector<CapabilityRequirement> capabilityRequirements =
        ruleset.requiredCapabilities;
    for (const auto& selectedAlgorithm : output.algorithms)
    {
        const AlgorithmDescriptor* algorithm = algorithms.Find(
            selectedAlgorithm.first,
            selectedAlgorithm.second
        );
        if (algorithm != nullptr)
        {
            capabilityRequirements.insert(
                capabilityRequirements.end(),
                algorithm->requiredCapabilities.begin(),
                algorithm->requiredCapabilities.end()
            );
        }
    }
    for (const CapabilityRequirement& requirement : capabilityRequirements)
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
                requirement.canonicalName,
                "selected Runtime Capability could not be resolved"
            );
            return false;
        }
        output.capabilities.emplace(
            resolved.capability,
            resolved.version
        );
    }
    return true;
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

    if (!RejectAmbiguousCapabilityProviders(packageLock, report))
    {
        return false;
    }

    RuntimeCompileSelection selection;
    if (!BuildCompileSelection(
            ruleset,
            packageLock,
            algorithms,
            definitions,
            entityDefinitions,
            relationDefinitions,
            mechanismSpawns,
            capabilityContracts,
            selection,
            report))
    {
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

    candidate.layouts_.reserve(selection.schemas.size());
    for (const auto& selectedSchema : selection.schemas)
    {
        const MechanismSchema& schema = *schemas.Find(
            selectedSchema.first,
            selectedSchema.second
        );
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

    candidate.relationLayouts_.reserve(selection.relations.size());
    for (const auto& selectedRelation : selection.relations)
    {
        const RelationSchema& schema = *relationSchemas.Find(
            selectedRelation.first,
            selectedRelation.second
        );
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

    candidate.componentLayouts_.reserve(selection.components.size());
    for (const auto& selectedComponent : selection.components)
    {
        const ComponentSchema& schema = *componentSchemas.Find(
            selectedComponent.first,
            selectedComponent.second
        );
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
    // A Component Type may appear at exactly ONE Schema version in a composed
    // Ruleset.
    //
    // Field Slots are assigned per (type, version) by sorted field name, so
    // two versions of one type give slot 3 two different meanings. Every
    // instruction that reaches a Component through a role carries the type and
    // the slot but no version -- it cannot carry one, because the Entity it
    // will write is not known until run time, and different Entities may
    // declare different versions of the same Component. Under two versions
    // such an instruction is not merely unchecked, it is unanswerable.
    //
    // This is the same ruling RejectAmbiguousCapabilityProviders makes for
    // contract providers, for the same reason: the ambiguity is rejected
    // rather than resolved by a rule nobody can see. Deliberately strict --
    // a later per-Entity version model can relax this; it could not tighten
    // it once content depends on the looseness.
    for (std::size_t index = 1;
        index < candidate.componentLayouts_.size();
        ++index)
    {
        const CompiledComponentLayout& previous =
            candidate.componentLayouts_[index - 1];
        const CompiledComponentLayout& current =
            candidate.componentLayouts_[index];
        if (previous.type == current.type)
        {
            AddIssue(
                report,
                RuntimeCompileIssueCode::ComponentSchemaVersionAmbiguous,
                componentSchemas.Find(current.type, current.schemaVersion)
                    ->canonicalName,
                "Component Type is selected at two Schema versions ("
                    + std::to_string(previous.schemaVersion) + " and "
                    + std::to_string(current.schemaVersion)
                    + "); a composed Ruleset may select only one"
            );
            return false;
        }
    }
    candidate.RebuildIndexes();

    candidate.definitions_.reserve(selection.definitions.size());
    for (MechanismDefinitionId selectedDefinition : selection.definitions)
    {
        const MechanismDefinition& definition =
            *definitions.Find(selectedDefinition);
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
        for (const CapabilityProvisionDeclaration& declaration
            : definition.providedCapabilities)
        {
            const RuntimeCapabilityContract* contract =
                ResolveCapabilityVersion(
                    capabilityContracts,
                    packageLock,
                    StableCapabilityId(declaration.capabilityName),
                    declaration.versions
                );
            if (contract == nullptr)
            {
                AddIssue(
                    report,
                    RuntimeCompileIssueCode::IntegrityValidationFailed,
                    definition.canonicalName,
                    "provides_capabilities names a Capability Contract with "
                    "no version inside the declared range"
                );
                return false;
            }
            compiled.providedCapabilities.push_back({
                contract->id,
                contract->canonicalName,
                contract->version
            });
        }
        std::sort(
            compiled.providedCapabilities.begin(),
            compiled.providedCapabilities.end(),
            [](const CapabilityProvision& first,
               const CapabilityProvision& second)
            {
                if (first.capability != second.capability)
                {
                    return first.capability < second.capability;
                }
                return first.version < second.version;
            }
        );
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

    candidate.entityDefinitions_.reserve(selection.entityDefinitions.size());
    for (EntityDefinitionId selectedDefinition
        : selection.entityDefinitions)
    {
        const EntityDefinition& definition =
            *entityDefinitions.Find(selectedDefinition);
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

    candidate.relationDefinitions_.reserve(
        selection.relationDefinitions.size()
    );
    for (RelationDefinitionId selectedDefinition
        : selection.relationDefinitions)
    {
        const RelationDefinition& definition =
            *relationDefinitions.Find(selectedDefinition);
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

    candidate.spawnDefinitions_.reserve(selection.spawns.size());
    for (MechanismSpawnDefinitionId selectedSpawn : selection.spawns)
    {
        const MechanismSpawnDefinition& spawn =
            *mechanismSpawns.Find(selectedSpawn);
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
        if (layout == nullptr)
        {
            AddIssue(
                report,
                RuntimeCompileIssueCode::SpawnDefinitionMissing,
                spawn.canonicalName,
                "Spawn has no compiled Mechanism Layout"
            );
            return false;
        }
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
    for (const auto& selectedCapability : selection.capabilities)
    {
        candidate.capabilities_.push_back(*capabilityContracts.Find(
            selectedCapability.first,
            selectedCapability.second
        ));
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
    candidate.algorithmCapabilityBindings_.reserve(
        selection.algorithms.size()
    );
    candidate.algorithms_.reserve(selection.algorithms.size());
    for (const auto& selectedAlgorithm : selection.algorithms)
    {
        const AlgorithmDescriptor& algorithm = *algorithms.Find(
            selectedAlgorithm.first,
            selectedAlgorithm.second
        );
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
        candidate.algorithms_.push_back(algorithm);
    }
    std::sort(
        candidate.algorithms_.begin(),
        candidate.algorithms_.end(),
        [](const AlgorithmDescriptor& first,
           const AlgorithmDescriptor& second)
        {
            return first.id != second.id
                ? first.id < second.id
                : first.version < second.version;
        }
    );
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
            || (algorithm->backend != AlgorithmBackend::Declarative
                && algorithm->backend != AlgorithmBackend::Script))
        {
            continue;
        }
        const CompiledMechanismLayout* layout = candidate.FindLayout(
            definition.type,
            definition.schemaVersion
        );
        if (algorithm->backend == AlgorithmBackend::Script)
        {
            if (layout == nullptr
                || !IsValidControlledScriptProgram(
                    algorithm->script,
                    algorithm->entryPoints))
            {
                AddIssue(
                    report,
                    RuntimeCompileIssueCode::AlgorithmProgramMissing,
                    algorithm->canonicalName,
                    "Controlled Script has no valid source program"
                );
                return false;
            }

            CompiledControlledScriptProgram program;
            program.definition = definition.id;
            program.algorithm = definition.algorithm;
            program.algorithmVersion = definition.algorithmVersion;
            for (std::size_t stateIndex = 0;
                stateIndex < algorithm->script.state.size();
                ++stateIndex)
            {
                const ControlledScriptStateDefinition& sourceState =
                    algorithm->script.state[stateIndex];
                program.stateSlotsByName.emplace(
                    sourceState.name,
                    static_cast<std::uint32_t>(stateIndex)
                );
                program.initialState.push_back(sourceState.initialValue);
            }
            if (ControlledScriptStateFootprint(program.initialState)
                > algorithm->executionPolicy.scriptMemoryLimitBytes)
            {
                AddIssue(
                    report,
                    RuntimeCompileIssueCode::AlgorithmProgramBudgetExceeded,
                    algorithm->canonicalName,
                    "Controlled Script initial state exceeds its memory quota"
                );
                return false;
            }

            // Lowers one Controlled Script `Transact` action to a declarative
            // bytecode instruction. Mirrors the declarative lowering below;
            // the RUNTIME half is genuinely shared -- both VMs execute the
            // result through runtime::EmitBytecodeTransaction -- so the two
            // backends cannot drift in behaviour, only (harmlessly) in which
            // compile diagnostic fires first.
            const auto lowerTransact =
                [&](const AlgorithmInstructionDefinition& src,
                    AlgorithmBytecodeInstruction& out) -> bool
            {
                for (const AlgorithmConditionDefinition& sourceCondition
                    : src.conditions)
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
                        == AlgorithmConditionKind::Compare)
                    {
                        condition.compare = sourceCondition.compare;
                        if (!LowerReadPath(
                                sourceCondition.left,
                                *layout,
                                candidate.layouts_,
                                candidate.componentLayouts_,
                                candidate.relationLayouts_,
                                algorithm->canonicalName,
                                report,
                                condition.left)
                            || !LowerReadPath(
                                sourceCondition.right,
                                *layout,
                                candidate.layouts_,
                                candidate.componentLayouts_,
                                candidate.relationLayouts_,
                                algorithm->canonicalName,
                                report,
                                condition.right))
                        {
                            return false;
                        }
                    }
                    else if (sourceCondition.kind
                        == AlgorithmConditionKind::SelfFieldEquals)
                    {
                        const auto conditionField =
                            layout->fieldSlotsByName.find(sourceCondition.field);
                        if (conditionField == layout->fieldSlotsByName.end()
                            || !MechanismValueMatchesSchema(
                                layout->fields[conditionField->second.value],
                                sourceCondition.value))
                        {
                            AddIssue(
                                report,
                                RuntimeCompileIssueCode::
                                    AlgorithmProgramOperandInvalid,
                                algorithm->canonicalName,
                                "controlled script condition references an "
                                "invalid field"
                            );
                            return false;
                        }
                        condition.field = conditionField->second;
                    }
                    out.conditions.push_back(std::move(condition));
                }
                switch (src.kind)
                {
                case AlgorithmInstructionKind::CancelEventsByType:
                    out.opcode = AlgorithmBytecodeOpcode::CancelEventsByType;
                    out.eventType = src.eventType;
                    return true;
                case AlgorithmInstructionKind::SetFieldComputed:
                case AlgorithmInstructionKind::AddFieldComputed:
                {
                    // Controlled Script reaches computed assignment through
                    // the same Transact lowering the declarative backend uses,
                    // so the two backends stay equivalent (memo 4.1 item 9c).
                    const auto target =
                        layout->fieldSlotsByName.find(src.field);
                    if (target == layout->fieldSlotsByName.end())
                    {
                        AddIssue(
                            report,
                            RuntimeCompileIssueCode::
                                AlgorithmProgramOperandInvalid,
                            algorithm->canonicalName,
                            "computed assignment names an unknown field"
                        );
                        return false;
                    }
                    const MechanismValueKind destination =
                        layout->fields[target->second.value].kind;
                    if (destination != MechanismValueKind::Integer
                        && destination != MechanismValueKind::Decimal)
                    {
                        AddIssue(
                            report,
                            RuntimeCompileIssueCode::
                                AlgorithmProgramOperandInvalid,
                            algorithm->canonicalName,
                            "computed assignment requires a numeric field"
                        );
                        return false;
                    }
                    if (!LowerReadPath(
                            src.left,
                            *layout,
                            candidate.layouts_,
                            candidate.componentLayouts_,
                            candidate.relationLayouts_,
                            algorithm->canonicalName,
                            report,
                            out.left))
                    {
                        return false;
                    }
                    out.hasRight = src.hasRight;
                    if (src.hasRight
                        && !LowerReadPath(
                            src.right,
                            *layout,
                            candidate.layouts_,
                            candidate.componentLayouts_,
                            candidate.relationLayouts_,
                            algorithm->canonicalName,
                            report,
                            out.right))
                    {
                        return false;
                    }
                    out.binaryOperator = src.binaryOperator;
                    out.field = target->second;
                    out.opcode = src.kind
                            == AlgorithmInstructionKind::SetFieldComputed
                        ? AlgorithmBytecodeOpcode::SetFieldComputed
                        : AlgorithmBytecodeOpcode::AddFieldComputed;
                    return true;
                }
                case AlgorithmInstructionKind::TransitionLifecycle:
                    out.opcode = AlgorithmBytecodeOpcode::TransitionLifecycle;
                    out.lifecycle = src.lifecycle;
                    return true;
                case AlgorithmInstructionKind::CreateEntity:
                    if (candidate.FindEntityDefinition(src.entityDefinition)
                        == nullptr)
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
                    out.opcode = AlgorithmBytecodeOpcode::CreateEntity;
                    out.entityDefinition = src.entityDefinition;
                    return true;
                case AlgorithmInstructionKind::AddRelation:
                {
                    const bool relationKnown = std::any_of(
                        candidate.relationLayouts_.begin(),
                        candidate.relationLayouts_.end(),
                        [&src](const CompiledRelationLayout& relation)
                        {
                            return relation.type == src.relationType;
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
                    out.opcode = AlgorithmBytecodeOpcode::AddRelation;
                    out.relationType = src.relationType;
                    out.sourceEntity = src.sourceEntity;
                    out.targetEntity = src.targetEntity;
                    return true;
                }
                case AlgorithmInstructionKind::RemoveRelation:
                    out.opcode = AlgorithmBytecodeOpcode::RemoveRelation;
                    out.relation = src.relation;
                    return true;
                case AlgorithmInstructionKind::SpawnMechanism:
                    if (candidate.FindSpawnDefinition(src.spawn) == nullptr)
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
                    out.opcode = AlgorithmBytecodeOpcode::SpawnMechanism;
                    out.spawn = src.spawn;
                    return true;
                case AlgorithmInstructionKind::ScheduleEvent:
                    if (src.dueTickOffset == 0)
                    {
                        AddIssue(
                            report,
                            RuntimeCompileIssueCode::
                                AlgorithmProgramOperandInvalid,
                            algorithm->canonicalName,
                            "schedule_event requires a positive tick delay"
                        );
                        return false;
                    }
                    out.opcode = AlgorithmBytecodeOpcode::ScheduleEvent;
                    out.eventType = src.eventType;
                    out.dueTickOffset = src.dueTickOffset;
                    out.priority = src.priority;
                    out.payload = src.payload;
                    return true;
                case AlgorithmInstructionKind::CancelEvent:
                    out.opcode = AlgorithmBytecodeOpcode::CancelEvent;
                    out.eventSequence = src.eventSequence;
                    return true;
                case AlgorithmInstructionKind::CreateRngStream:
                    out.opcode = AlgorithmBytecodeOpcode::CreateRngStream;
                    out.rngStream = src.rngStream;
                    out.rngSeed = src.rngSeed;
                    return true;
                case AlgorithmInstructionKind::AdvanceRngStream:
                    out.opcode = AlgorithmBytecodeOpcode::AdvanceRngStream;
                    out.rngStream = src.rngStream;
                    out.rngCount = src.rngCount;
                    return true;
                case AlgorithmInstructionKind::SetComponentField:
                case AlgorithmInstructionKind::SetComponentFieldComputed:
                {
                    const bool computed = src.kind
                        == AlgorithmInstructionKind::SetComponentFieldComputed;
                    const CompiledEntityDefinition* ownerDefinition = nullptr;
                    for (const CompiledEntityDefinition& entity
                        : candidate.entityDefinitions_)
                    {
                        if (StableEntityId(entity.id) == src.entity)
                        {
                            ownerDefinition = &entity;
                            break;
                        }
                    }
                    const CompiledEntityComponentDefinition* component = nullptr;
                    if (ownerDefinition != nullptr)
                    {
                        for (const CompiledEntityComponentDefinition& entry
                            : ownerDefinition->components)
                        {
                            if (entry.type == src.component)
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
                            src.componentField
                        );
                    // The literal form checks the value against the schema
                    // here. The computed form has no value yet, so what is
                    // checked instead is that the destination can hold an
                    // arithmetic result at all -- the same rule the computed
                    // Mechanism field assignment applies.
                    if (componentLayout == nullptr
                        || componentField
                            == componentLayout->fieldSlotsByName.end()
                        || (!computed
                            && !MechanismValueMatchesSchema(
                                componentLayout->fields[
                                    componentField->second.value],
                                src.operand)))
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
                    out.entity = src.entity;
                    out.component = src.component;
                    out.componentField = componentField->second;
                    if (!computed)
                    {
                        out.opcode =
                            AlgorithmBytecodeOpcode::SetComponentFieldConstant;
                        out.operand = src.operand;
                        return true;
                    }
                    const MechanismValueKind destination =
                        componentLayout->fields[
                            componentField->second.value].kind;
                    if (destination != MechanismValueKind::Integer
                        && destination != MechanismValueKind::Decimal)
                    {
                        AddIssue(
                            report,
                            RuntimeCompileIssueCode::
                                AlgorithmProgramOperandInvalid,
                            algorithm->canonicalName,
                            "computed set_component_field requires a numeric "
                            "Component field"
                        );
                        return false;
                    }
                    if (!LowerReadPath(
                            src.left,
                            *layout,
                            candidate.layouts_,
                            candidate.componentLayouts_,
                            candidate.relationLayouts_,
                            algorithm->canonicalName,
                            report,
                            out.left))
                    {
                        return false;
                    }
                    out.hasRight = src.hasRight;
                    if (src.hasRight
                        && !LowerReadPath(
                            src.right,
                            *layout,
                            candidate.layouts_,
                            candidate.componentLayouts_,
                            candidate.relationLayouts_,
                            algorithm->canonicalName,
                            report,
                            out.right))
                    {
                        return false;
                    }
                    out.binaryOperator = src.binaryOperator;
                    out.componentFieldKind = destination;
                    out.opcode =
                        AlgorithmBytecodeOpcode::SetComponentFieldComputed;
                    return true;
                }
                case AlgorithmInstructionKind::SetComponentFieldByRole:
                case AlgorithmInstructionKind::SetComponentFieldByRoleComputed:
                {
                    const bool computed = src.kind
                        == AlgorithmInstructionKind::
                            SetComponentFieldByRoleComputed;
                    const auto roleSlot =
                        layout->roleSlotsByName.find(src.targetRoleName);
                    if (roleSlot == layout->roleSlotsByName.end())
                    {
                        AddIssue(
                            report,
                            RuntimeCompileIssueCode::
                                AlgorithmProgramOperandInvalid,
                            algorithm->canonicalName,
                            "set_component_field names an unknown role"
                        );
                        return false;
                    }
                    // The slot has to hold Entities. A Mechanism Instance
                    // reference has no Components, so writing one would fail
                    // at run time on every single invocation.
                    if (layout->roles[roleSlot->second.value].referenceKind
                        != MechanismReferenceKind::Entity)
                    {
                        AddIssue(
                            report,
                            RuntimeCompileIssueCode::
                                AlgorithmProgramOperandInvalid,
                            algorithm->canonicalName,
                            "set_component_field requires a role that "
                            "references Entities"
                        );
                        return false;
                    }
                    // Resolved by type alone, which is exact because a
                    // composed Ruleset selects one version per Component Type
                    // (ComponentSchemaVersionAmbiguous). Which Entity ends up
                    // in the slot is a Content decision, so there is no Entity
                    // Definition to check the field against here -- the
                    // Component schema is the contract both sides share.
                    const CompiledComponentLayout* componentLayout = nullptr;
                    for (const CompiledComponentLayout& compiled
                        : candidate.componentLayouts_)
                    {
                        if (compiled.type == src.component)
                        {
                            componentLayout = &compiled;
                            break;
                        }
                    }
                    const auto componentField = componentLayout == nullptr
                        ? std::map<std::string, ComponentFieldSlotId>::
                            const_iterator{}
                        : componentLayout->fieldSlotsByName.find(
                            src.componentField
                        );
                    if (componentLayout == nullptr
                        || componentField
                            == componentLayout->fieldSlotsByName.end()
                        || (!computed
                            && !MechanismValueMatchesSchema(
                                componentLayout->fields[
                                    componentField->second.value],
                                src.operand)))
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
                    out.targetRoleSlot = roleSlot->second;
                    out.component = src.component;
                    out.componentField = componentField->second;
                    if (!computed)
                    {
                        out.opcode = AlgorithmBytecodeOpcode::
                            SetComponentFieldByRoleConstant;
                        out.operand = src.operand;
                        return true;
                    }
                    const MechanismValueKind destination =
                        componentLayout->fields[
                            componentField->second.value].kind;
                    if (destination != MechanismValueKind::Integer
                        && destination != MechanismValueKind::Decimal)
                    {
                        AddIssue(
                            report,
                            RuntimeCompileIssueCode::
                                AlgorithmProgramOperandInvalid,
                            algorithm->canonicalName,
                            "computed set_component_field requires a numeric "
                            "Component field"
                        );
                        return false;
                    }
                    if (!LowerReadPath(
                            src.left,
                            *layout,
                            candidate.layouts_,
                            candidate.componentLayouts_,
                            candidate.relationLayouts_,
                            algorithm->canonicalName,
                            report,
                            out.left))
                    {
                        return false;
                    }
                    out.hasRight = src.hasRight;
                    if (src.hasRight
                        && !LowerReadPath(
                            src.right,
                            *layout,
                            candidate.layouts_,
                            candidate.componentLayouts_,
                            candidate.relationLayouts_,
                            algorithm->canonicalName,
                            report,
                            out.right))
                    {
                        return false;
                    }
                    out.binaryOperator = src.binaryOperator;
                    out.componentFieldKind = destination;
                    out.opcode = AlgorithmBytecodeOpcode::
                        SetComponentFieldByRoleComputed;
                    return true;
                }
                case AlgorithmInstructionKind::InvokeCapability:
                {
                    if (src.dueTickOffset == 0)
                    {
                        AddIssue(
                            report,
                            RuntimeCompileIssueCode::
                                AlgorithmProgramOperandInvalid,
                            algorithm->canonicalName,
                            "invoke_capability requires a positive tick delay"
                        );
                        return false;
                    }
                    const CapabilityId capabilityId = StableCapabilityId(
                        src.capabilityName
                    );
                    const RuntimeCapabilityContract* resolved = nullptr;
                    for (const RuntimeCapabilityContract& contract
                        : candidate.capabilities_)
                    {
                        if (contract.id != capabilityId
                            || !src.capabilityVersions.Contains(
                                contract.version))
                        {
                            continue;
                        }
                        if (resolved == nullptr
                            || contract.version > resolved->version)
                        {
                            resolved = &contract;
                        }
                    }
                    if (resolved == nullptr)
                    {
                        AddIssue(
                            report,
                            RuntimeCompileIssueCode::
                                AlgorithmProgramOperandInvalid,
                            algorithm->canonicalName,
                            "invoke_capability references a Capability version "
                            "that is not part of the composed Ruleset"
                        );
                        return false;
                    }
                    MechanismRoleSlotId targetRoleSlot;
                    if (!src.targetRoleName.empty())
                    {
                        const auto roleSlot = layout->roleSlotsByName.find(
                            src.targetRoleName
                        );
                        if (roleSlot == layout->roleSlotsByName.end())
                        {
                            AddIssue(
                                report,
                                RuntimeCompileIssueCode::
                                    AlgorithmProgramOperandInvalid,
                                algorithm->canonicalName,
                                "invoke_capability target_role has no compiled "
                                "role slot on the invoking Definition: "
                                    + src.targetRoleName
                            );
                            return false;
                        }
                        targetRoleSlot = roleSlot->second;
                    }
                    out.opcode = AlgorithmBytecodeOpcode::InvokeCapability;
                    out.capability = capabilityId;
                    out.capabilityVersion = resolved->version;
                    out.targetRoleSlot = targetRoleSlot;
                    out.capabilityDeliveryType =
                        CapabilityDeliveryEventType(src.capabilityName);
                    out.dueTickOffset = src.dueTickOffset;
                    out.priority = src.priority;
                    out.payload = src.payload;
                    out.payloadComputed = src.payloadComputed;
                    if (src.payloadComputed
                        && !LowerReadPath(
                            src.payloadSource,
                            *layout,
                            candidate.layouts_,
                            candidate.componentLayouts_,
                            candidate.relationLayouts_,
                            algorithm->canonicalName,
                            report,
                            out.payloadSource))
                    {
                        return false;
                    }
                    return true;
                }
                case AlgorithmInstructionKind::SetField:
                case AlgorithmInstructionKind::AddField:
                    break;
                }

                const auto actionField = layout->fieldSlotsByName.find(
                    src.field
                );
                if (actionField == layout->fieldSlotsByName.end())
                {
                    AddIssue(
                        report,
                        RuntimeCompileIssueCode::AlgorithmProgramFieldMissing,
                        algorithm->canonicalName,
                        "controlled script field has no compiled Runtime Slot: "
                            + src.field
                    );
                    return false;
                }
                out.field = actionField->second;
                out.operand = src.operand;
                out.operandFromPayload = src.operandFromPayload;
                const MechanismFieldSchema& actionSchema =
                    layout->fields[actionField->second.value];
                if (src.kind == AlgorithmInstructionKind::SetField)
                {
                    if (!out.operandFromPayload
                        && !MechanismValueMatchesSchema(
                            actionSchema,
                            out.operand))
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
                    out.opcode = AlgorithmBytecodeOpcode::SetFieldConstant;
                    return true;
                }
                if (out.operandFromPayload)
                {
                    if (actionSchema.kind == MechanismValueKind::Integer)
                    {
                        out.opcode =
                            AlgorithmBytecodeOpcode::AddIntegerConstant;
                        return true;
                    }
                    if (actionSchema.kind == MechanismValueKind::Decimal)
                    {
                        out.opcode =
                            AlgorithmBytecodeOpcode::AddDecimalConstant;
                        return true;
                    }
                    AddIssue(
                        report,
                        RuntimeCompileIssueCode::AlgorithmProgramOperandInvalid,
                        algorithm->canonicalName,
                        "add_field from_payload requires a numeric field"
                    );
                    return false;
                }
                if (actionSchema.kind == MechanismValueKind::Integer
                    && out.operand.Kind() == MechanismValueKind::Integer)
                {
                    out.opcode = AlgorithmBytecodeOpcode::AddIntegerConstant;
                    return true;
                }
                if (actionSchema.kind == MechanismValueKind::Decimal
                    && (out.operand.Kind() == MechanismValueKind::Decimal
                        || out.operand.Kind() == MechanismValueKind::Integer))
                {
                    if (out.operand.Kind() == MechanismValueKind::Integer)
                    {
                        out.operand = MechanismValue(
                            static_cast<double>(
                                std::get<std::int64_t>(out.operand.data))
                        );
                    }
                    out.opcode = AlgorithmBytecodeOpcode::AddDecimalConstant;
                    return true;
                }
                AddIssue(
                    report,
                    RuntimeCompileIssueCode::AlgorithmProgramOperandInvalid,
                    algorithm->canonicalName,
                    "add_field requires a compatible numeric field"
                );
                return false;
            };

            for (const auto& sourceStage : algorithm->script.stages)
            {
                std::vector<ControlledScriptInstruction> bytecode;
                bytecode.reserve(sourceStage.second.size());
                for (const ControlledScriptInstructionDefinition& source
                    : sourceStage.second)
                {
                    ControlledScriptInstruction instruction;
                    instruction.operand = source.operand;
                    instruction.lifecycle = source.lifecycle;
                    instruction.targetInstruction = source.targetInstruction;
                    const auto state = program.stateSlotsByName.find(
                        source.state
                    );
                    const auto field = layout->fieldSlotsByName.find(
                        source.field
                    );
                    switch (source.kind)
                    {
                    case ControlledScriptInstructionKind::Transact:
                        if (!lowerTransact(source.action, instruction.transact))
                        {
                            return false;
                        }
                        instruction.opcode = ControlledScriptOpcode::Transact;
                        break;
                    case ControlledScriptInstructionKind::SetState:
                        if (state == program.stateSlotsByName.end()
                            || source.operand.Kind()
                                != program.initialState[state->second].Kind())
                        {
                            AddIssue(
                                report,
                                RuntimeCompileIssueCode::
                                    AlgorithmProgramOperandInvalid,
                                algorithm->canonicalName,
                                "set_state violates its persistent state type"
                            );
                            return false;
                        }
                        instruction.opcode =
                            ControlledScriptOpcode::SetStateConstant;
                        instruction.stateSlot = state->second;
                        break;
                    case ControlledScriptInstructionKind::AddState:
                        if (state == program.stateSlotsByName.end())
                        {
                            AddIssue(
                                report,
                                RuntimeCompileIssueCode::
                                    AlgorithmProgramOperandInvalid,
                                algorithm->canonicalName,
                                "add_state references an unknown state slot"
                            );
                            return false;
                        }
                        instruction.stateSlot = state->second;
                        if (program.initialState[state->second].Kind()
                                == MechanismValueKind::Integer
                            && source.operand.Kind()
                                == MechanismValueKind::Integer)
                        {
                            instruction.opcode = ControlledScriptOpcode::
                                AddStateIntegerConstant;
                        }
                        else if (program.initialState[state->second].Kind()
                                == MechanismValueKind::Decimal
                            && (source.operand.Kind()
                                    == MechanismValueKind::Decimal
                                || source.operand.Kind()
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
                            instruction.opcode = ControlledScriptOpcode::
                                AddStateDecimalConstant;
                        }
                        else
                        {
                            AddIssue(
                                report,
                                RuntimeCompileIssueCode::
                                    AlgorithmProgramOperandInvalid,
                                algorithm->canonicalName,
                                "add_state requires a compatible numeric state"
                            );
                            return false;
                        }
                        break;
                    case ControlledScriptInstructionKind::SetField:
                    case ControlledScriptInstructionKind::AddField:
                        if (field == layout->fieldSlotsByName.end())
                        {
                            AddIssue(
                                report,
                                RuntimeCompileIssueCode::
                                    AlgorithmProgramFieldMissing,
                                algorithm->canonicalName,
                                "Controlled Script references an unknown field"
                            );
                            return false;
                        }
                        instruction.field = field->second;
                        if (source.kind
                            == ControlledScriptInstructionKind::SetField)
                        {
                            if (!MechanismValueMatchesSchema(
                                    layout->fields[field->second.value],
                                    source.operand))
                            {
                                AddIssue(
                                    report,
                                    RuntimeCompileIssueCode::
                                        AlgorithmProgramOperandInvalid,
                                    algorithm->canonicalName,
                                    "set_field violates the target field schema"
                                );
                                return false;
                            }
                            instruction.opcode = ControlledScriptOpcode::
                                SetFieldConstant;
                        }
                        else if (layout->fields[field->second.value].kind
                                == MechanismValueKind::Integer
                            && source.operand.Kind()
                                == MechanismValueKind::Integer)
                        {
                            instruction.opcode = ControlledScriptOpcode::
                                AddFieldIntegerConstant;
                        }
                        else if (layout->fields[field->second.value].kind
                                == MechanismValueKind::Decimal
                            && (source.operand.Kind()
                                    == MechanismValueKind::Decimal
                                || source.operand.Kind()
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
                            instruction.opcode = ControlledScriptOpcode::
                                AddFieldDecimalConstant;
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
                        break;
                    case ControlledScriptInstructionKind::TransitionLifecycle:
                        instruction.opcode = ControlledScriptOpcode::
                            TransitionLifecycle;
                        break;
                    case ControlledScriptInstructionKind::Jump:
                        instruction.opcode = ControlledScriptOpcode::Jump;
                        break;
                    case ControlledScriptInstructionKind::JumpIfStateEquals:
                        if (state == program.stateSlotsByName.end()
                            || source.operand.Kind()
                                != program.initialState[state->second].Kind())
                        {
                            AddIssue(
                                report,
                                RuntimeCompileIssueCode::
                                    AlgorithmProgramOperandInvalid,
                                algorithm->canonicalName,
                                "jump_if_state_equals violates its state type"
                            );
                            return false;
                        }
                        instruction.opcode = ControlledScriptOpcode::
                            JumpIfStateEquals;
                        instruction.stateSlot = state->second;
                        break;
                    case ControlledScriptInstructionKind::Yield:
                        instruction.opcode = ControlledScriptOpcode::Yield;
                        break;
                    case ControlledScriptInstructionKind::Halt:
                        instruction.opcode = ControlledScriptOpcode::Halt;
                        break;
                    }
                    bytecode.push_back(std::move(instruction));
                }
                program.stages.emplace(
                    sourceStage.first,
                    std::move(bytecode)
                );
            }
            candidate.controlledScriptPrograms_.push_back(
                std::move(program)
            );
            continue;
        }
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
                        == AlgorithmConditionKind::Compare)
                    {
                        condition.compare = sourceCondition.compare;
                        if (!LowerReadPath(
                                sourceCondition.left,
                                *layout,
                                candidate.layouts_,
                                candidate.componentLayouts_,
                                candidate.relationLayouts_,
                                algorithm->canonicalName,
                                report,
                                condition.left)
                            || !LowerReadPath(
                                sourceCondition.right,
                                *layout,
                                candidate.layouts_,
                                candidate.componentLayouts_,
                                candidate.relationLayouts_,
                                algorithm->canonicalName,
                                report,
                                condition.right))
                        {
                            return false;
                        }
                    }
                    else if (sourceCondition.kind
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
                    == AlgorithmInstructionKind::CancelEventsByType)
                {
                    instruction.opcode =
                        AlgorithmBytecodeOpcode::CancelEventsByType;
                    instruction.eventType = sourceInstruction.eventType;
                    bytecode.push_back(std::move(instruction));
                    continue;
                }

                if (sourceInstruction.kind
                        == AlgorithmInstructionKind::SetFieldComputed
                    || sourceInstruction.kind
                        == AlgorithmInstructionKind::AddFieldComputed)
                {
                    const auto target = layout->fieldSlotsByName.find(
                        sourceInstruction.field
                    );
                    if (target == layout->fieldSlotsByName.end())
                    {
                        AddIssue(
                            report,
                            RuntimeCompileIssueCode::
                                AlgorithmProgramOperandInvalid,
                            algorithm->canonicalName,
                            "computed assignment names an unknown field"
                        );
                        return false;
                    }
                    const MechanismValueKind destination =
                        layout->fields[target->second.value].kind;
                    if (destination != MechanismValueKind::Integer
                        && destination != MechanismValueKind::Decimal)
                    {
                        AddIssue(
                            report,
                            RuntimeCompileIssueCode::
                                AlgorithmProgramOperandInvalid,
                            algorithm->canonicalName,
                            "computed assignment requires a numeric field"
                        );
                        return false;
                    }
                    if (!LowerReadPath(
                            sourceInstruction.left,
                            *layout,
                            candidate.layouts_,
                            candidate.componentLayouts_,
                            candidate.relationLayouts_,
                            algorithm->canonicalName,
                            report,
                            instruction.left))
                    {
                        return false;
                    }
                    instruction.hasRight = sourceInstruction.hasRight;
                    if (sourceInstruction.hasRight
                        && !LowerReadPath(
                            sourceInstruction.right,
                            *layout,
                            candidate.layouts_,
                            candidate.componentLayouts_,
                            candidate.relationLayouts_,
                            algorithm->canonicalName,
                            report,
                            instruction.right))
                    {
                        return false;
                    }
                    instruction.binaryOperator =
                        sourceInstruction.binaryOperator;
                    instruction.field = target->second;
                    instruction.opcode = sourceInstruction.kind
                            == AlgorithmInstructionKind::SetFieldComputed
                        ? AlgorithmBytecodeOpcode::SetFieldComputed
                        : AlgorithmBytecodeOpcode::AddFieldComputed;
                    bytecode.push_back(std::move(instruction));
                    continue;
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
                        == AlgorithmInstructionKind::SetComponentField
                    || sourceInstruction.kind
                        == AlgorithmInstructionKind::SetComponentFieldComputed)
                {
                    const bool computed = sourceInstruction.kind
                        == AlgorithmInstructionKind::SetComponentFieldComputed;
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
                    // The literal form checks the value against the schema
                    // here. The computed form has no value yet, so what is
                    // checked instead is that the destination can hold an
                    // arithmetic result at all.
                    if (componentLayout == nullptr
                        || componentField
                            == componentLayout->fieldSlotsByName.end()
                        || (!computed
                            && !MechanismValueMatchesSchema(
                                componentLayout->fields[
                                    componentField->second.value],
                                sourceInstruction.operand)))
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
                    instruction.entity = sourceInstruction.entity;
                    instruction.component = sourceInstruction.component;
                    instruction.componentField = componentField->second;
                    if (!computed)
                    {
                        instruction.opcode = AlgorithmBytecodeOpcode::
                            SetComponentFieldConstant;
                        instruction.operand = sourceInstruction.operand;
                        bytecode.push_back(std::move(instruction));
                        continue;
                    }
                    const MechanismValueKind destination =
                        componentLayout->fields[
                            componentField->second.value].kind;
                    if (destination != MechanismValueKind::Integer
                        && destination != MechanismValueKind::Decimal)
                    {
                        AddIssue(
                            report,
                            RuntimeCompileIssueCode::
                                AlgorithmProgramOperandInvalid,
                            algorithm->canonicalName,
                            "computed set_component_field requires a numeric "
                            "Component field"
                        );
                        return false;
                    }
                    if (!LowerReadPath(
                            sourceInstruction.left,
                            *layout,
                            candidate.layouts_,
                            candidate.componentLayouts_,
                            candidate.relationLayouts_,
                            algorithm->canonicalName,
                            report,
                            instruction.left))
                    {
                        return false;
                    }
                    instruction.hasRight = sourceInstruction.hasRight;
                    if (sourceInstruction.hasRight
                        && !LowerReadPath(
                            sourceInstruction.right,
                            *layout,
                            candidate.layouts_,
                            candidate.componentLayouts_,
                            candidate.relationLayouts_,
                            algorithm->canonicalName,
                            report,
                            instruction.right))
                    {
                        return false;
                    }
                    instruction.binaryOperator =
                        sourceInstruction.binaryOperator;
                    instruction.componentFieldKind = destination;
                    instruction.opcode = AlgorithmBytecodeOpcode::
                        SetComponentFieldComputed;
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
                    if (sourceInstruction.dueTickOffset == 0)
                    {
                        AddIssue(
                            report,
                            RuntimeCompileIssueCode::
                                AlgorithmProgramOperandInvalid,
                            algorithm->canonicalName,
                            "schedule_event requires a positive tick delay"
                        );
                        return false;
                    }
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
                if (sourceInstruction.kind
                        == AlgorithmInstructionKind::SetComponentFieldByRole
                    || sourceInstruction.kind
                        == AlgorithmInstructionKind::
                            SetComponentFieldByRoleComputed)
                {
                    const bool computed = sourceInstruction.kind
                        == AlgorithmInstructionKind::
                            SetComponentFieldByRoleComputed;
                    const auto roleSlot = layout->roleSlotsByName.find(
                        sourceInstruction.targetRoleName
                    );
                    if (roleSlot == layout->roleSlotsByName.end())
                    {
                        AddIssue(
                            report,
                            RuntimeCompileIssueCode::
                                AlgorithmProgramOperandInvalid,
                            algorithm->canonicalName,
                            "set_component_field names an unknown role"
                        );
                        return false;
                    }
                    // The slot has to hold Entities. A Mechanism Instance
                    // reference has no Components, so writing one would fail
                    // at run time on every single invocation.
                    if (layout->roles[roleSlot->second.value].referenceKind
                        != MechanismReferenceKind::Entity)
                    {
                        AddIssue(
                            report,
                            RuntimeCompileIssueCode::
                                AlgorithmProgramOperandInvalid,
                            algorithm->canonicalName,
                            "set_component_field requires a role that "
                            "references Entities"
                        );
                        return false;
                    }
                    // Resolved by Component type alone, which is exact
                    // because a composed Ruleset selects one version per
                    // Component Type (ComponentSchemaVersionAmbiguous). Which
                    // Entity lands in the slot is a Content decision; the
                    // Component schema is the contract the Mechanism and the
                    // Content share, and it is the only thing a reusable
                    // mechanism may depend on.
                    const CompiledComponentLayout* componentLayout = nullptr;
                    for (const CompiledComponentLayout& compiled
                        : candidate.componentLayouts_)
                    {
                        if (compiled.type == sourceInstruction.component)
                        {
                            componentLayout = &compiled;
                            break;
                        }
                    }
                    const auto componentField = componentLayout == nullptr
                        ? std::map<std::string, ComponentFieldSlotId>::
                            const_iterator{}
                        : componentLayout->fieldSlotsByName.find(
                            sourceInstruction.componentField
                        );
                    if (componentLayout == nullptr
                        || componentField
                            == componentLayout->fieldSlotsByName.end()
                        || (!computed
                            && !MechanismValueMatchesSchema(
                                componentLayout->fields[
                                    componentField->second.value],
                                sourceInstruction.operand)))
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
                    instruction.targetRoleSlot = roleSlot->second;
                    instruction.component = sourceInstruction.component;
                    instruction.componentField = componentField->second;
                    if (!computed)
                    {
                        instruction.opcode = AlgorithmBytecodeOpcode::
                            SetComponentFieldByRoleConstant;
                        instruction.operand = sourceInstruction.operand;
                        bytecode.push_back(std::move(instruction));
                        continue;
                    }
                    const MechanismValueKind destination =
                        componentLayout->fields[
                            componentField->second.value].kind;
                    if (destination != MechanismValueKind::Integer
                        && destination != MechanismValueKind::Decimal)
                    {
                        AddIssue(
                            report,
                            RuntimeCompileIssueCode::
                                AlgorithmProgramOperandInvalid,
                            algorithm->canonicalName,
                            "computed set_component_field requires a numeric "
                            "Component field"
                        );
                        return false;
                    }
                    if (!LowerReadPath(
                            sourceInstruction.left,
                            *layout,
                            candidate.layouts_,
                            candidate.componentLayouts_,
                            candidate.relationLayouts_,
                            algorithm->canonicalName,
                            report,
                            instruction.left))
                    {
                        return false;
                    }
                    instruction.hasRight = sourceInstruction.hasRight;
                    if (sourceInstruction.hasRight
                        && !LowerReadPath(
                            sourceInstruction.right,
                            *layout,
                            candidate.layouts_,
                            candidate.componentLayouts_,
                            candidate.relationLayouts_,
                            algorithm->canonicalName,
                            report,
                            instruction.right))
                    {
                        return false;
                    }
                    instruction.binaryOperator =
                        sourceInstruction.binaryOperator;
                    instruction.componentFieldKind = destination;
                    instruction.opcode = AlgorithmBytecodeOpcode::
                        SetComponentFieldByRoleComputed;
                    bytecode.push_back(std::move(instruction));
                    continue;
                }
                if (sourceInstruction.kind
                    == AlgorithmInstructionKind::InvokeCapability)
                {
                    if (sourceInstruction.dueTickOffset == 0)
                    {
                        AddIssue(
                            report,
                            RuntimeCompileIssueCode::
                                AlgorithmProgramOperandInvalid,
                            algorithm->canonicalName,
                            "invoke_capability requires a positive tick delay"
                        );
                        return false;
                    }
                    const CapabilityId capabilityId = StableCapabilityId(
                        sourceInstruction.capabilityName
                    );
                    const RuntimeCapabilityContract* resolved = nullptr;
                    for (const RuntimeCapabilityContract& contract
                        : candidate.capabilities_)
                    {
                        if (contract.id != capabilityId
                            || !sourceInstruction.capabilityVersions.Contains(
                                contract.version))
                        {
                            continue;
                        }
                        if (resolved == nullptr
                            || contract.version > resolved->version)
                        {
                            resolved = &contract;
                        }
                    }
                    if (resolved == nullptr)
                    {
                        AddIssue(
                            report,
                            RuntimeCompileIssueCode::
                                AlgorithmProgramOperandInvalid,
                            algorithm->canonicalName,
                            "invoke_capability references a Capability version "
                            "that is not part of the composed Ruleset"
                        );
                        return false;
                    }
                    MechanismRoleSlotId targetRoleSlot;
                    if (!sourceInstruction.targetRoleName.empty())
                    {
                        const auto roleSlot = layout->roleSlotsByName.find(
                            sourceInstruction.targetRoleName
                        );
                        if (roleSlot == layout->roleSlotsByName.end())
                        {
                            AddIssue(
                                report,
                                RuntimeCompileIssueCode::
                                    AlgorithmProgramOperandInvalid,
                                algorithm->canonicalName,
                                "invoke_capability target_role has no compiled "
                                "role slot on the invoking Definition: "
                                    + sourceInstruction.targetRoleName
                            );
                            return false;
                        }
                        targetRoleSlot = roleSlot->second;
                    }
                    instruction.opcode =
                        AlgorithmBytecodeOpcode::InvokeCapability;
                    instruction.capability = capabilityId;
                    instruction.capabilityVersion = resolved->version;
                    instruction.targetRoleSlot = targetRoleSlot;
                    instruction.capabilityDeliveryType =
                        CapabilityDeliveryEventType(
                            sourceInstruction.capabilityName
                        );
                    instruction.dueTickOffset =
                        sourceInstruction.dueTickOffset;
                    instruction.priority = sourceInstruction.priority;
                    instruction.payload = sourceInstruction.payload;
                    instruction.payloadComputed =
                        sourceInstruction.payloadComputed;
                    if (sourceInstruction.payloadComputed
                        && !LowerReadPath(
                            sourceInstruction.payloadSource,
                            *layout,
                            candidate.layouts_,
                            candidate.componentLayouts_,
                            candidate.relationLayouts_,
                            algorithm->canonicalName,
                            report,
                            instruction.payloadSource))
                    {
                        return false;
                    }
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
                instruction.operandFromPayload =
                    sourceInstruction.operandFromPayload;
                const MechanismFieldSchema& fieldSchema =
                    layout->fields[field->second.value];
                if (sourceInstruction.kind
                    == AlgorithmInstructionKind::SetField)
                {
                    if (!instruction.operandFromPayload
                        && !MechanismValueMatchesSchema(
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
                else if (instruction.operandFromPayload)
                {
                    if (fieldSchema.kind == MechanismValueKind::Integer)
                    {
                        instruction.opcode =
                            AlgorithmBytecodeOpcode::AddIntegerConstant;
                    }
                    else if (fieldSchema.kind == MechanismValueKind::Decimal)
                    {
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
                            "add_field from_payload requires a numeric field"
                        );
                        return false;
                    }
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
    std::sort(
        candidate.controlledScriptPrograms_.begin(),
        candidate.controlledScriptPrograms_.end(),
        [](const CompiledControlledScriptProgram& first,
           const CompiledControlledScriptProgram& second)
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
