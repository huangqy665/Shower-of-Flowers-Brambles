#include "frozen_runtime_catalog.hpp"

namespace dillen::kernel {

bool FrozenRuntimeCatalog::IsFrozen() const noexcept
{
    return frozen_;
}

RulesetId FrozenRuntimeCatalog::ActiveRuleset() const noexcept
{
    return ruleset_;
}

std::uint32_t FrozenRuntimeCatalog::ActiveRulesetVersion() const noexcept
{
    return rulesetVersion_;
}

const std::vector<AppliedRulesetExtension>&
FrozenRuntimeCatalog::RulesetExtensions() const noexcept
{
    return rulesetExtensions_;
}

RulesetFingerprint FrozenRuntimeCatalog::Fingerprint() const noexcept
{
    return fingerprint_;
}

const PackageLock& FrozenRuntimeCatalog::LockedPackages() const noexcept
{
    return packageLock_;
}

const SourceLock& FrozenRuntimeCatalog::LockedSources() const noexcept
{
    return sourceLock_;
}

std::size_t FrozenRuntimeCatalog::LayoutCount() const noexcept
{
    return layouts_.size();
}

std::size_t FrozenRuntimeCatalog::DefinitionCount() const noexcept
{
    return definitions_.size();
}

std::size_t FrozenRuntimeCatalog::AlgorithmCount() const noexcept
{
    return algorithms_.size();
}

std::size_t FrozenRuntimeCatalog::ComponentLayoutCount() const noexcept
{
    return componentLayouts_.size();
}

std::size_t FrozenRuntimeCatalog::EntityDefinitionCount() const noexcept
{
    return entityDefinitions_.size();
}

std::size_t FrozenRuntimeCatalog::RelationLayoutCount() const noexcept
{
    return relationLayouts_.size();
}

std::size_t FrozenRuntimeCatalog::RelationDefinitionCount() const noexcept
{
    return relationDefinitions_.size();
}

std::size_t FrozenRuntimeCatalog::SpawnDefinitionCount() const noexcept
{
    return spawnDefinitions_.size();
}

std::size_t FrozenRuntimeCatalog::CapabilityCount() const noexcept
{
    return capabilities_.size();
}

std::size_t FrozenRuntimeCatalog::AlgorithmProgramCount() const noexcept
{
    return algorithmPrograms_.size();
}

std::size_t FrozenRuntimeCatalog::ControlledScriptProgramCount() const noexcept
{
    return controlledScriptPrograms_.size();
}

const std::vector<CompiledMechanismDefinition>&
FrozenRuntimeCatalog::Definitions() const noexcept
{
    return definitions_;
}

const std::vector<CompiledEntityDefinition>&
FrozenRuntimeCatalog::EntityDefinitions() const noexcept
{
    return entityDefinitions_;
}

const std::vector<CompiledRelationDefinition>&
FrozenRuntimeCatalog::RelationDefinitions() const noexcept
{
    return relationDefinitions_;
}

const std::vector<CompiledMechanismSpawnDefinition>&
FrozenRuntimeCatalog::SpawnDefinitions() const noexcept
{
    return spawnDefinitions_;
}

const CompiledMechanismLayout* FrozenRuntimeCatalog::FindLayout(
    MechanismTypeId type,
    std::uint32_t schemaVersion
) const
{
    const auto iterator = layoutIndex_.find({type.value, schemaVersion});
    return iterator == layoutIndex_.end()
        ? nullptr
        : &layouts_[iterator->second];
}

const CompiledMechanismDefinition* FrozenRuntimeCatalog::FindDefinition(
    MechanismDefinitionId definition
) const
{
    const auto iterator = definitionIndex_.find(definition.value);
    return iterator == definitionIndex_.end()
        ? nullptr
        : &definitions_[iterator->second];
}

const AlgorithmDescriptor* FrozenRuntimeCatalog::FindAlgorithm(
    AlgorithmId algorithm,
    std::uint32_t version
) const
{
    const auto iterator = algorithmIndex_.find({algorithm.value, version});
    return iterator == algorithmIndex_.end()
        ? nullptr
        : &algorithms_[iterator->second];
}

const CompiledAlgorithmProgram* FrozenRuntimeCatalog::FindAlgorithmProgram(
    MechanismDefinitionId definition
) const
{
    const auto iterator = algorithmProgramIndex_.find(definition.value);
    return iterator == algorithmProgramIndex_.end()
        ? nullptr
        : &algorithmPrograms_[iterator->second];
}

const CompiledControlledScriptProgram*
FrozenRuntimeCatalog::FindControlledScriptProgram(
    MechanismDefinitionId definition
) const
{
    const auto iterator = controlledScriptProgramIndex_.find(
        definition.value
    );
    return iterator == controlledScriptProgramIndex_.end()
        ? nullptr
        : &controlledScriptPrograms_[iterator->second];
}

const RuntimeCapabilityContract* FrozenRuntimeCatalog::FindCapability(
    CapabilityId capability,
    std::uint32_t version
) const
{
    const auto iterator = capabilityIndex_.find({capability.value, version});
    return iterator == capabilityIndex_.end()
        ? nullptr
        : &capabilities_[iterator->second];
}

const RuntimeCapabilityContract* FrozenRuntimeCatalog::FindCapability(
    CapabilityBindingSlotId slot
) const
{
    return slot && slot.value < capabilities_.size()
        ? &capabilities_[slot.value]
        : nullptr;
}

const std::vector<CapabilityBindingSlotId>&
FrozenRuntimeCatalog::AlgorithmCapabilities(
    AlgorithmId algorithm,
    std::uint32_t version
) const
{
    static const std::vector<CapabilityBindingSlotId> empty;
    const auto iterator = algorithmCapabilityIndex_.find({
        algorithm.value,
        version
    });
    return iterator == algorithmCapabilityIndex_.end()
        ? empty
        : algorithmCapabilityBindings_[iterator->second].capabilities;
}

const CompiledComponentLayout* FrozenRuntimeCatalog::FindComponentLayout(
    ComponentTypeId type,
    std::uint32_t schemaVersion
) const
{
    const auto iterator = componentLayoutIndex_.find({
        type.value,
        schemaVersion
    });
    return iterator == componentLayoutIndex_.end()
        ? nullptr
        : &componentLayouts_[iterator->second];
}

const CompiledEntityDefinition* FrozenRuntimeCatalog::FindEntityDefinition(
    EntityDefinitionId definition
) const
{
    const auto iterator = entityDefinitionIndex_.find(definition.value);
    return iterator == entityDefinitionIndex_.end()
        ? nullptr
        : &entityDefinitions_[iterator->second];
}

const CompiledRelationLayout* FrozenRuntimeCatalog::FindRelationLayout(
    RelationTypeId type,
    std::uint32_t schemaVersion
) const
{
    const auto iterator = relationLayoutIndex_.find({
        type.value,
        schemaVersion
    });
    return iterator == relationLayoutIndex_.end()
        ? nullptr
        : &relationLayouts_[iterator->second];
}

const CompiledRelationLayout* FrozenRuntimeCatalog::FindRelationLayout(
    RelationTypeId type
) const
{
    const CompiledRelationLayout* result = nullptr;
    for (const CompiledRelationLayout& layout : relationLayouts_)
    {
        if (layout.type == type
            && (result == nullptr
                || result->schemaVersion < layout.schemaVersion))
        {
            result = &layout;
        }
    }
    return result;
}

const CompiledRelationDefinition*
FrozenRuntimeCatalog::FindRelationDefinition(
    RelationDefinitionId definition
) const
{
    const auto iterator = relationDefinitionIndex_.find(definition.value);
    return iterator == relationDefinitionIndex_.end()
        ? nullptr
        : &relationDefinitions_[iterator->second];
}

const CompiledMechanismSpawnDefinition*
FrozenRuntimeCatalog::FindSpawnDefinition(
    MechanismSpawnDefinitionId spawn
) const
{
    const auto iterator = spawnDefinitionIndex_.find(spawn.value);
    return iterator == spawnDefinitionIndex_.end()
        ? nullptr
        : &spawnDefinitions_[iterator->second];
}

std::optional<MechanismFieldSlotId> FrozenRuntimeCatalog::ResolveFieldSlot(
    MechanismTypeId type,
    std::uint32_t schemaVersion,
    std::string_view name
) const
{
    const CompiledMechanismLayout* layout = FindLayout(
        type,
        schemaVersion
    );
    if (layout == nullptr)
    {
        return std::nullopt;
    }
    const auto iterator = layout->fieldSlotsByName.find(std::string(name));
    return iterator == layout->fieldSlotsByName.end()
        ? std::nullopt
        : std::optional<MechanismFieldSlotId>(iterator->second);
}

std::optional<MechanismRoleSlotId> FrozenRuntimeCatalog::ResolveRoleSlot(
    MechanismTypeId type,
    std::uint32_t schemaVersion,
    std::string_view name
) const
{
    const CompiledMechanismLayout* layout = FindLayout(
        type,
        schemaVersion
    );
    if (layout == nullptr)
    {
        return std::nullopt;
    }
    const auto iterator = layout->roleSlotsByName.find(std::string(name));
    return iterator == layout->roleSlotsByName.end()
        ? std::nullopt
        : std::optional<MechanismRoleSlotId>(iterator->second);
}

std::optional<MechanismFieldSlotId>
FrozenRuntimeCatalog::ResolveDefinitionFieldSlot(
    MechanismDefinitionId definition,
    std::string_view name
) const
{
    const CompiledMechanismDefinition* compiled = FindDefinition(definition);
    return compiled == nullptr
        ? std::nullopt
        : ResolveFieldSlot(
            compiled->type,
            compiled->schemaVersion,
            name
        );
}

std::optional<ComponentFieldSlotId>
FrozenRuntimeCatalog::ResolveComponentFieldSlot(
    ComponentTypeId type,
    std::uint32_t schemaVersion,
    std::string_view name
) const
{
    const CompiledComponentLayout* layout = FindComponentLayout(
        type,
        schemaVersion
    );
    if (layout == nullptr)
    {
        return std::nullopt;
    }
    const auto iterator = layout->fieldSlotsByName.find(std::string(name));
    return iterator == layout->fieldSlotsByName.end()
        ? std::nullopt
        : std::optional<ComponentFieldSlotId>(iterator->second);
}

std::string_view FrozenRuntimeCatalog::FieldName(
    MechanismTypeId type,
    std::uint32_t schemaVersion,
    MechanismFieldSlotId slot
) const
{
    const CompiledMechanismLayout* layout = FindLayout(
        type,
        schemaVersion
    );
    return layout != nullptr && slot.value < layout->fields.size()
        ? std::string_view(layout->fields[slot.value].name)
        : std::string_view{};
}

void FrozenRuntimeCatalog::RebuildIndexes()
{
    layoutIndex_.clear();
    definitionIndex_.clear();
    componentLayoutIndex_.clear();
    entityDefinitionIndex_.clear();
    relationLayoutIndex_.clear();
    relationDefinitionIndex_.clear();
    spawnDefinitionIndex_.clear();
    algorithmIndex_.clear();
    algorithmProgramIndex_.clear();
    controlledScriptProgramIndex_.clear();
    capabilityIndex_.clear();
    algorithmCapabilityIndex_.clear();
    for (std::size_t index = 0; index < layouts_.size(); ++index)
    {
        layoutIndex_[{
            layouts_[index].type.value,
            layouts_[index].schemaVersion
        }] = index;
    }
    for (std::size_t index = 0; index < definitions_.size(); ++index)
    {
        definitionIndex_[definitions_[index].id.value] = index;
    }
    for (std::size_t index = 0; index < componentLayouts_.size(); ++index)
    {
        componentLayoutIndex_[{
            componentLayouts_[index].type.value,
            componentLayouts_[index].schemaVersion
        }] = index;
    }
    for (std::size_t index = 0; index < entityDefinitions_.size(); ++index)
    {
        entityDefinitionIndex_[entityDefinitions_[index].id.value] = index;
    }
    for (std::size_t index = 0; index < relationLayouts_.size(); ++index)
    {
        relationLayoutIndex_[{
            relationLayouts_[index].type.value,
            relationLayouts_[index].schemaVersion
        }] = index;
    }
    for (std::size_t index = 0;
        index < relationDefinitions_.size();
        ++index)
    {
        relationDefinitionIndex_[
            relationDefinitions_[index].id.value
        ] = index;
    }
    for (std::size_t index = 0; index < spawnDefinitions_.size(); ++index)
    {
        spawnDefinitionIndex_[spawnDefinitions_[index].id.value] = index;
    }
    for (std::size_t index = 0; index < algorithms_.size(); ++index)
    {
        algorithmIndex_[{
            algorithms_[index].id.value,
            algorithms_[index].version
        }] = index;
    }
    for (std::size_t index = 0; index < algorithmPrograms_.size(); ++index)
    {
        algorithmProgramIndex_[
            algorithmPrograms_[index].definition.value
        ] = index;
    }
    for (std::size_t index = 0;
        index < controlledScriptPrograms_.size();
        ++index)
    {
        controlledScriptProgramIndex_.emplace(
            controlledScriptPrograms_[index].definition.value,
            index
        );
    }
    for (std::size_t index = 0; index < capabilities_.size(); ++index)
    {
        capabilityIndex_[{
            capabilities_[index].id.value,
            capabilities_[index].version
        }] = index;
    }
    for (std::size_t index = 0;
        index < algorithmCapabilityBindings_.size();
        ++index)
    {
        algorithmCapabilityIndex_[{
            algorithmCapabilityBindings_[index].algorithm.value,
            algorithmCapabilityBindings_[index].algorithmVersion
        }] = index;
    }
}

}
