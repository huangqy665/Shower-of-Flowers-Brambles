#pragma once

#include <string>
#include <vector>

#include "algorithm_registry.hpp"
#include "component_schema.hpp"
#include "entity_definition_registry.hpp"
#include "mechanism_definition_registry.hpp"
#include "mechanism_schema_registry.hpp"
#include "mechanism_spawn_definition_registry.hpp"
#include "package_lock.hpp"
#include "relation_definition_registry.hpp"
#include "ruleset.hpp"

namespace dillen::kernel {

enum class RulesetIntegrityIssueCode
{
    InvalidRuleset,
    PackageLockUnresolved,
    RegistryNotFrozen,
    RequiredPackageMissing,
    RequiredPackageVersionMismatch,
    LockedDependencyMissing,
    LockedDependencyVersionMismatch,
    RequiredSchemaMissing,
    RequiredComponentMissing,
    RequiredRelationMissing,
    RequiredDefinitionMissing,
    RequiredEntityDefinitionMissing,
    RequiredRelationDefinitionMissing,
    RequiredMechanismSpawnMissing,
    RequiredAlgorithmMissing,
    RequiredCapabilityMissing,
    PackageCapabilityContractMissing,
    DefinitionSchemaMissing,
    DefinitionAlgorithmMissing,
    EntityComponentSchemaMissing,
    SpawnDefinitionMissing,
    AlgorithmCapabilityMissing
};

struct RulesetIntegrityIssue
{
    RulesetIntegrityIssueCode code =
        RulesetIntegrityIssueCode::PackageLockUnresolved;
    std::string subject;
    std::string message;
};

struct RulesetIntegrityReport
{
    std::vector<RulesetIntegrityIssue> issues;

    bool Success() const noexcept;
};

class RulesetIntegrityValidator
{
public:
    bool Validate(
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
    ) const;
    bool Validate(
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
    ) const;
};

}
