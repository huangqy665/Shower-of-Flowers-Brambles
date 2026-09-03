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

// Lower one read path for a caller that is not an algorithm.
//
// A projection -- something that asks one question of each of many Entities --
// needs the same read path an algorithm uses, rooted at the Entity it is
// asking about. This is that lowering, against a frozen Catalog; the terminal
// is resolved by the same code the algorithm compiler uses, so there is one
// answer to what a read path means rather than two.
bool LowerSubjectReadPath(
    const AlgorithmReadPathDefinition& source,
    const FrozenRuntimeCatalog& catalog,
    // The Component schema version the path's terminal is read at. Declared by
    // the asset rather than guessed, for the same reason a Capability names
    // its version: two schema versions of one Component are two field layouts,
    // and picking the wrong one reads the wrong slot without failing.
    std::uint32_t componentVersion,
    std::string& message,
    CompiledAlgorithmReadPath& out
);

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
