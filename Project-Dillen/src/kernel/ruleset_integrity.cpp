#include "ruleset_integrity.hpp"

#include <set>

namespace dillen::kernel {

namespace {

void AddIssue(
    RulesetIntegrityReport& report,
    RulesetIntegrityIssueCode code,
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

std::string IdSubject(std::uint64_t value)
{
    return std::to_string(value);
}

}

bool RulesetIntegrityReport::Success() const noexcept
{
    return issues.empty();
}

bool RulesetIntegrityValidator::Validate(
    const RulesetDefinition& ruleset,
    const PackageLock& packageLock,
    const MechanismSchemaRegistry& schemas,
    const ComponentSchemaRegistry& componentSchemas,
    const AlgorithmRegistry& algorithms,
    const MechanismDefinitionRegistry& definitions,
    const EntityDefinitionRegistry& entityDefinitions,
    const MechanismSpawnDefinitionRegistry& mechanismSpawns,
    const RuntimeCapabilityContractRegistry& capabilityContracts,
    RulesetIntegrityReport& report
) const
{
    RelationSchemaRegistry relationSchemas;
    relationSchemas.Freeze();
    RelationDefinitionRegistry relationDefinitions;
    relationDefinitions.Freeze();
    return Validate(
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
        report
    );
}

bool RulesetIntegrityValidator::Validate(
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
    RulesetIntegrityReport& report
) const
{
    report = {};
    if (!IsValidRulesetDefinition(ruleset))
    {
        AddIssue(
            report,
            RulesetIntegrityIssueCode::InvalidRuleset,
            ruleset.canonicalName,
            "Ruleset definition is structurally invalid"
        );
    }
    if (!packageLock.IsResolved())
    {
        AddIssue(
            report,
            RulesetIntegrityIssueCode::PackageLockUnresolved,
            ruleset.canonicalName,
            "Ruleset has no resolved Package Lock"
        );
    }
    if (!schemas.IsFrozen())
    {
        AddIssue(
            report,
            RulesetIntegrityIssueCode::RegistryNotFrozen,
            "mechanism_schema_registry",
            "Mechanism Schema Registry is not frozen"
        );
    }
    if (!algorithms.IsFrozen())
    {
        AddIssue(
            report,
            RulesetIntegrityIssueCode::RegistryNotFrozen,
            "algorithm_registry",
            "Algorithm Registry is not frozen"
        );
    }
    if (!componentSchemas.IsFrozen())
    {
        AddIssue(
            report,
            RulesetIntegrityIssueCode::RegistryNotFrozen,
            "component_schema_registry",
            "Component Schema Registry is not frozen"
        );
    }
    if (!relationSchemas.IsFrozen())
    {
        AddIssue(
            report,
            RulesetIntegrityIssueCode::RegistryNotFrozen,
            "relation_schema_registry",
            "Relation Schema Registry is not frozen"
        );
    }
    if (!definitions.IsFrozen())
    {
        AddIssue(
            report,
            RulesetIntegrityIssueCode::RegistryNotFrozen,
            "mechanism_definition_registry",
            "Mechanism Definition Registry is not frozen"
        );
    }
    if (!entityDefinitions.IsFrozen())
    {
        AddIssue(
            report,
            RulesetIntegrityIssueCode::RegistryNotFrozen,
            "entity_definition_registry",
            "Entity Definition Registry is not frozen"
        );
    }
    if (!relationDefinitions.IsFrozen())
    {
        AddIssue(
            report,
            RulesetIntegrityIssueCode::RegistryNotFrozen,
            "relation_definition_registry",
            "Relation Definition Registry is not frozen"
        );
    }
    if (!mechanismSpawns.IsFrozen())
    {
        AddIssue(
            report,
            RulesetIntegrityIssueCode::RegistryNotFrozen,
            "mechanism_spawn_definition_registry",
            "Mechanism Spawn Definition Registry is not frozen"
        );
    }
    if (!capabilityContracts.IsFrozen())
    {
        AddIssue(
            report,
            RulesetIntegrityIssueCode::RegistryNotFrozen,
            "runtime_capability_contract_registry",
            "Runtime Capability Contract Registry is not frozen"
        );
    }
    if (!report.Success())
    {
        return false;
    }

    for (const RulesetPackageRequirement& requirement : ruleset.packages)
    {
        const PackageLockEntry* entry = packageLock.Find(
            requirement.package
        );
        if (entry == nullptr)
        {
            AddIssue(
                report,
                RulesetIntegrityIssueCode::RequiredPackageMissing,
                requirement.canonicalName,
                "Required package is absent from Package Lock"
            );
        }
        else if (!requirement.versions.Contains(entry->version))
        {
            AddIssue(
                report,
                RulesetIntegrityIssueCode::RequiredPackageVersionMismatch,
                requirement.canonicalName,
                "Locked package version violates Ruleset requirement"
            );
        }
    }
    for (const PackageLockEntry& entry : packageLock.Entries())
    {
        for (const LockedPackageDependency& dependency : entry.dependencies)
        {
            const PackageLockEntry* locked = packageLock.Find(
                dependency.package
            );
            if (locked == nullptr)
            {
                AddIssue(
                    report,
                    RulesetIntegrityIssueCode::LockedDependencyMissing,
                    entry.canonicalName,
                    "Locked package dependency is missing"
                );
            }
            else if (locked->version != dependency.version)
            {
                AddIssue(
                    report,
                    RulesetIntegrityIssueCode::LockedDependencyVersionMismatch,
                    entry.canonicalName,
                    "Locked dependency version does not match Package Lock"
                );
            }
        }
    }

    for (const RulesetSchemaRequirement& requirement
        : ruleset.requiredSchemas)
    {
        if (schemas.Find(requirement.type, requirement.version) == nullptr)
        {
            AddIssue(
                report,
                RulesetIntegrityIssueCode::RequiredSchemaMissing,
                IdSubject(requirement.type.value),
                "Required Mechanism Schema is missing"
            );
        }
    }
    for (MechanismDefinitionId requirement
        : ruleset.requiredDefinitions)
    {
        if (definitions.Find(requirement) == nullptr)
        {
            AddIssue(
                report,
                RulesetIntegrityIssueCode::RequiredDefinitionMissing,
                IdSubject(requirement.value),
                "Required Mechanism Definition is missing"
            );
        }
    }
    for (const RulesetComponentRequirement& requirement
        : ruleset.requiredComponents)
    {
        if (componentSchemas.Find(
                requirement.type,
                requirement.version) == nullptr)
        {
            AddIssue(
                report,
                RulesetIntegrityIssueCode::RequiredComponentMissing,
                IdSubject(requirement.type.value),
                "Required Component Schema is missing"
            );
        }
    }
    for (const RulesetRelationRequirement& requirement
        : ruleset.requiredRelations)
    {
        if (relationSchemas.Find(
                requirement.type,
                requirement.version) == nullptr)
        {
            AddIssue(
                report,
                RulesetIntegrityIssueCode::RequiredRelationMissing,
                IdSubject(requirement.type.value),
                "Required Relation Schema is missing"
            );
        }
    }
    for (EntityDefinitionId requirement
        : ruleset.requiredEntityDefinitions)
    {
        if (entityDefinitions.Find(requirement) == nullptr)
        {
            AddIssue(
                report,
                RulesetIntegrityIssueCode::RequiredEntityDefinitionMissing,
                IdSubject(requirement.value),
                "Required Entity Definition is missing"
            );
        }
    }
    for (RelationDefinitionId requirement
        : ruleset.requiredRelationDefinitions)
    {
        if (relationDefinitions.Find(requirement) == nullptr)
        {
            AddIssue(
                report,
                RulesetIntegrityIssueCode::RequiredRelationDefinitionMissing,
                IdSubject(requirement.value),
                "Required Relation Definition is missing"
            );
        }
    }
    for (MechanismSpawnDefinitionId requirement
        : ruleset.requiredMechanismSpawns)
    {
        if (mechanismSpawns.Find(requirement) == nullptr)
        {
            AddIssue(
                report,
                RulesetIntegrityIssueCode::RequiredMechanismSpawnMissing,
                IdSubject(requirement.value),
                "Required Mechanism Spawn Definition is missing"
            );
        }
    }
    for (const RulesetAlgorithmRequirement& requirement
        : ruleset.requiredAlgorithms)
    {
        if (algorithms.Find(
                requirement.algorithm,
                requirement.version) == nullptr)
        {
            AddIssue(
                report,
                RulesetIntegrityIssueCode::RequiredAlgorithmMissing,
                IdSubject(requirement.algorithm.value),
                "Required Algorithm is missing"
            );
        }
    }

    RuntimeCapabilityResolver capabilityResolver;
    for (const PackageLockEntry& entry : packageLock.Entries())
    {
        for (const CapabilityProvision& provision
            : entry.providedCapabilities)
        {
            const RuntimeCapabilityContract* contract =
                capabilityContracts.Find(
                    provision.capability,
                    provision.version
                );
            if (contract == nullptr
                || contract->canonicalName != provision.canonicalName)
            {
                AddIssue(
                    report,
                    RulesetIntegrityIssueCode::PackageCapabilityContractMissing,
                    provision.canonicalName,
                    "Package advertises an unavailable Capability Contract version"
                );
            }
        }
    }
    for (const CapabilityRequirement& capability
        : ruleset.requiredCapabilities)
    {
        ResolvedCapabilityContract resolved;
        if (capabilityResolver.Resolve(
                capability,
                capabilityContracts,
                packageLock,
                resolved) != CapabilityResolveResult::Resolved)
        {
            AddIssue(
                report,
                RulesetIntegrityIssueCode::RequiredCapabilityMissing,
                capability.canonicalName,
                "Required Runtime Capability has no compatible Contract version"
            );
        }
    }

    for (const MechanismDefinition& definition : definitions.All())
    {
        if (schemas.Find(definition.type, definition.schemaVersion) == nullptr)
        {
            AddIssue(
                report,
                RulesetIntegrityIssueCode::DefinitionSchemaMissing,
                definition.canonicalName,
                "Definition references an unavailable Schema"
            );
        }
        if (definition.algorithm)
        {
            const AlgorithmDescriptor* algorithm = algorithms.Find(
                definition.algorithm,
                definition.algorithmVersion
            );
            if (algorithm == nullptr)
            {
                AddIssue(
                    report,
                    RulesetIntegrityIssueCode::DefinitionAlgorithmMissing,
                    definition.canonicalName,
                    "Definition references an unavailable Algorithm"
                );
            }
        }
    }
    for (const AlgorithmDescriptor& algorithm : algorithms.All())
    {
        for (const CapabilityRequirement& capability
            : algorithm.requiredCapabilities)
        {
            ResolvedCapabilityContract resolved;
            if (capabilityResolver.Resolve(
                    capability,
                    capabilityContracts,
                    packageLock,
                    resolved) != CapabilityResolveResult::Resolved)
            {
                AddIssue(
                    report,
                    RulesetIntegrityIssueCode::AlgorithmCapabilityMissing,
                    algorithm.canonicalName,
                    "Algorithm requires unavailable capability: "
                        + capability.canonicalName
                );
            }
        }
    }
    for (const EntityDefinition& definition : entityDefinitions.All())
    {
        for (const EntityComponentDefinition& component
            : definition.components)
        {
            if (componentSchemas.Find(
                    component.type,
                    component.schemaVersion) == nullptr)
            {
                AddIssue(
                    report,
                    RulesetIntegrityIssueCode::EntityComponentSchemaMissing,
                    definition.canonicalName,
                    "Entity Definition references an unavailable Component Schema"
                );
            }
        }
    }
    for (const MechanismSpawnDefinition& spawn : mechanismSpawns.All())
    {
        if (definitions.Find(spawn.definition) == nullptr)
        {
            AddIssue(
                report,
                RulesetIntegrityIssueCode::SpawnDefinitionMissing,
                spawn.canonicalName,
                "Mechanism Spawn references an unavailable Definition"
            );
        }
    }
    return report.Success();
}

}
