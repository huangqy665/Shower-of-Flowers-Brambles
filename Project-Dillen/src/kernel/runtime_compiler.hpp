#pragma once

#include <string>
#include <vector>

#include "frozen_runtime_catalog.hpp"
#include "entity_definition_registry.hpp"
#include "mechanism_spawn_definition_registry.hpp"
#include "relation_definition_registry.hpp"
#include "ruleset_integrity.hpp"

namespace dillen::kernel {

enum class RuntimeCompileIssueCode
{
    IntegrityValidationFailed,
    SlotCapacityExceeded,
    DefinitionLayoutMissing,
    DefinitionFieldMissing,
    DefinitionRoleMissing,
    EntityComponentLayoutMissing,
    EntityComponentFieldMissing,
    SpawnDefinitionMissing,
    SpawnFieldMissing,
    SpawnRoleMissing,
    AlgorithmProgramMissing,
    AlgorithmProgramBudgetExceeded,
    AlgorithmProgramFieldMissing,
    AlgorithmProgramOperandInvalid,
    // Two Schema versions of one Component Type were selected. Slots are
    // assigned per (type, version), so the same slot number would mean two
    // different fields; an instruction that reaches a Component through a role
    // carries no version and could not choose between them.
    ComponentSchemaVersionAmbiguous
};

struct RuntimeCompileIssue
{
    RuntimeCompileIssueCode code =
        RuntimeCompileIssueCode::IntegrityValidationFailed;
    std::string subject;
    std::string message;
};

struct RuntimeCompileReport
{
    RulesetIntegrityReport integrity;
    std::vector<RuntimeCompileIssue> issues;

    bool Success() const noexcept;
};

class RuntimeCompiler
{
public:
    bool Compile(
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
    ) const;
    bool Compile(
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
    ) const;
    bool Compile(
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
    ) const;
};

}
