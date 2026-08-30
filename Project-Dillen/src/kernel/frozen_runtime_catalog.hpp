#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "algorithm_registry.hpp"
#include "component_schema.hpp"
#include "entity_definition.hpp"
#include "mechanism_definition.hpp"
#include "mechanism_schema.hpp"
#include "mechanism_spawn_definition.hpp"
#include "package_lock.hpp"
#include "relation_definition.hpp"
#include "relation_schema.hpp"
#include "ruleset_fingerprint.hpp"
#include "runtime_capability_contract.hpp"
#include "source_lock.hpp"

namespace dillen::kernel {

struct CompiledMechanismLayout
{
    MechanismTypeId type;
    std::uint32_t schemaVersion = 0;
    std::vector<MechanismFieldSchema> fields;
    std::vector<MechanismRoleSchema> roles;
    std::map<std::string, MechanismFieldSlotId> fieldSlotsByName;
    std::map<std::string, MechanismRoleSlotId> roleSlotsByName;
};

struct CompiledMechanismDefinition
{
    MechanismDefinitionId id;
    MechanismTypeId type;
    std::uint32_t schemaVersion = 0;
    AlgorithmId algorithm;
    std::uint32_t algorithmVersion = 0;
    std::vector<MechanismValue> initialValues;
    std::vector<std::vector<MechanismReference>> initialRoles;
    // Resolved Capability Contracts this Definition's instances provide,
    // in stable (id, version) order.
    std::vector<CapabilityProvision> providedCapabilities;
};

struct CompiledComponentLayout
{
    ComponentTypeId type;
    std::uint32_t schemaVersion = 0;
    std::vector<MechanismFieldSchema> fields;
    std::map<std::string, ComponentFieldSlotId> fieldSlotsByName;
};

struct CompiledEntityComponentDefinition
{
    ComponentTypeId type;
    std::uint32_t schemaVersion = 0;
    std::vector<MechanismValue> initialValues;
};

struct CompiledEntityDefinition
{
    EntityDefinitionId id;
    EntityTypeId type;
    std::vector<CompiledEntityComponentDefinition> components;
};

struct CompiledRelationLayout
{
    RelationTypeId type;
    std::uint32_t schemaVersion = 0;
    std::optional<EntityTypeId> sourceType;
    std::optional<EntityTypeId> targetType;
    bool allowSelf = false;
};

struct CompiledRelationDefinition
{
    RelationDefinitionId id;
    RelationTypeId type;
    std::uint32_t schemaVersion = 0;
    EntityDefinitionId source;
    EntityDefinitionId target;
};

struct CompiledMechanismSpawnDefinition
{
    MechanismSpawnDefinitionId id;
    MechanismDefinitionId definition;
    std::uint32_t count = 1;
    std::vector<MechanismValue> initialValues;
    std::vector<std::vector<MechanismReference>> initialRoles;
};

struct CompiledAlgorithmCapabilityBinding
{
    AlgorithmId algorithm;
    std::uint32_t algorithmVersion = 0;
    std::vector<CapabilityBindingSlotId> capabilities;
};

class FrozenRuntimeCatalog
{
public:
    bool IsFrozen() const noexcept;
    RulesetId ActiveRuleset() const noexcept;
    std::uint32_t ActiveRulesetVersion() const noexcept;
    const std::vector<AppliedRulesetExtension>& RulesetExtensions()
        const noexcept;
    RulesetFingerprint Fingerprint() const noexcept;
    const PackageLock& LockedPackages() const noexcept;
    const SourceLock& LockedSources() const noexcept;
    std::size_t LayoutCount() const noexcept;
    std::size_t DefinitionCount() const noexcept;
    std::size_t AlgorithmCount() const noexcept;
    std::size_t ComponentLayoutCount() const noexcept;
    std::size_t EntityDefinitionCount() const noexcept;
    std::size_t RelationLayoutCount() const noexcept;
    std::size_t RelationDefinitionCount() const noexcept;
    std::size_t SpawnDefinitionCount() const noexcept;
    std::size_t CapabilityCount() const noexcept;
    std::size_t AlgorithmProgramCount() const noexcept;
    std::size_t ControlledScriptProgramCount() const noexcept;
    const std::vector<CompiledMechanismDefinition>& Definitions()
        const noexcept;
    const std::vector<CompiledEntityDefinition>& EntityDefinitions()
        const noexcept;
    const std::vector<CompiledRelationDefinition>& RelationDefinitions()
        const noexcept;
    const std::vector<CompiledMechanismSpawnDefinition>& SpawnDefinitions()
        const noexcept;

    const CompiledMechanismLayout* FindLayout(
        MechanismTypeId type,
        std::uint32_t schemaVersion
    ) const;
    const CompiledMechanismDefinition* FindDefinition(
        MechanismDefinitionId definition
    ) const;
    const AlgorithmDescriptor* FindAlgorithm(
        AlgorithmId algorithm,
        std::uint32_t version
    ) const;
    const CompiledAlgorithmProgram* FindAlgorithmProgram(
        MechanismDefinitionId definition
    ) const;
    const CompiledControlledScriptProgram* FindControlledScriptProgram(
        MechanismDefinitionId definition
    ) const;
    const RuntimeCapabilityContract* FindCapability(
        CapabilityId capability,
        std::uint32_t version
    ) const;
    const RuntimeCapabilityContract* FindCapability(
        CapabilityBindingSlotId slot
    ) const;
    const std::vector<CapabilityBindingSlotId>& AlgorithmCapabilities(
        AlgorithmId algorithm,
        std::uint32_t version
    ) const;
    const CompiledComponentLayout* FindComponentLayout(
        ComponentTypeId type,
        std::uint32_t schemaVersion
    ) const;
    const CompiledEntityDefinition* FindEntityDefinition(
        EntityDefinitionId definition
    ) const;
    const CompiledRelationLayout* FindRelationLayout(
        RelationTypeId type,
        std::uint32_t schemaVersion
    ) const;
    const CompiledRelationLayout* FindRelationLayout(
        RelationTypeId type
    ) const;
    const CompiledRelationDefinition* FindRelationDefinition(
        RelationDefinitionId definition
    ) const;
    const CompiledMechanismSpawnDefinition* FindSpawnDefinition(
        MechanismSpawnDefinitionId spawn
    ) const;
    std::optional<MechanismFieldSlotId> ResolveFieldSlot(
        MechanismTypeId type,
        std::uint32_t schemaVersion,
        std::string_view name
    ) const;
    std::optional<MechanismRoleSlotId> ResolveRoleSlot(
        MechanismTypeId type,
        std::uint32_t schemaVersion,
        std::string_view name
    ) const;
    std::optional<MechanismFieldSlotId> ResolveDefinitionFieldSlot(
        MechanismDefinitionId definition,
        std::string_view name
    ) const;
    std::optional<ComponentFieldSlotId> ResolveComponentFieldSlot(
        ComponentTypeId type,
        std::uint32_t schemaVersion,
        std::string_view name
    ) const;
    std::string_view FieldName(
        MechanismTypeId type,
        std::uint32_t schemaVersion,
        MechanismFieldSlotId slot
    ) const;

private:
    friend class RuntimeCompiler;

    void RebuildIndexes();

    RulesetId ruleset_;
    std::uint32_t rulesetVersion_ = 0;
    std::vector<AppliedRulesetExtension> rulesetExtensions_;
    RulesetFingerprint fingerprint_;
    PackageLock packageLock_;
    SourceLock sourceLock_;
    std::vector<CompiledMechanismLayout> layouts_;
    std::vector<CompiledMechanismDefinition> definitions_;
    std::vector<CompiledComponentLayout> componentLayouts_;
    std::vector<CompiledEntityDefinition> entityDefinitions_;
    std::vector<CompiledRelationLayout> relationLayouts_;
    std::vector<CompiledRelationDefinition> relationDefinitions_;
    std::vector<CompiledMechanismSpawnDefinition> spawnDefinitions_;
    std::vector<AlgorithmDescriptor> algorithms_;
    std::vector<CompiledAlgorithmProgram> algorithmPrograms_;
    std::vector<CompiledControlledScriptProgram> controlledScriptPrograms_;
    std::vector<RuntimeCapabilityContract> capabilities_;
    std::vector<CompiledAlgorithmCapabilityBinding>
        algorithmCapabilityBindings_;
    std::map<std::pair<std::uint64_t, std::uint32_t>, std::size_t>
        layoutIndex_;
    std::map<std::uint64_t, std::size_t> definitionIndex_;
    std::map<std::pair<std::uint64_t, std::uint32_t>, std::size_t>
        componentLayoutIndex_;
    std::map<std::uint64_t, std::size_t> entityDefinitionIndex_;
    std::map<std::pair<std::uint64_t, std::uint32_t>, std::size_t>
        relationLayoutIndex_;
    std::map<std::uint64_t, std::size_t> relationDefinitionIndex_;
    std::map<std::uint64_t, std::size_t> spawnDefinitionIndex_;
    std::map<std::pair<std::uint64_t, std::uint32_t>, std::size_t>
        algorithmIndex_;
    std::map<std::uint64_t, std::size_t> algorithmProgramIndex_;
    std::map<std::uint64_t, std::size_t> controlledScriptProgramIndex_;
    std::map<std::pair<std::uint64_t, std::uint32_t>, std::size_t>
        capabilityIndex_;
    std::map<std::pair<std::uint64_t, std::uint32_t>, std::size_t>
        algorithmCapabilityIndex_;
    bool frozen_ = false;
};

}
