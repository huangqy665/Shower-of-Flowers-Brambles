#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "mechanism_ids.hpp"
#include "package_manifest.hpp"
#include "runtime_capability_contract.hpp"

namespace dillen::kernel {

struct RulesetPackageRequirement
{
    PackageId package;
    std::string canonicalName;
    PackageVersionRange versions;
};

struct RulesetSchemaRequirement
{
    MechanismTypeId type;
    std::uint32_t version = 0;
};

struct RulesetAlgorithmRequirement
{
    AlgorithmId algorithm;
    std::uint32_t version = 0;
};

struct RulesetComponentRequirement
{
    ComponentTypeId type;
    std::uint32_t version = 0;
};

struct RulesetRelationRequirement
{
    RelationTypeId type;
    std::uint32_t version = 0;
};

struct RulesetRequirementSet
{
    std::vector<RulesetPackageRequirement> packages;
    std::vector<RulesetSchemaRequirement> requiredSchemas;
    std::vector<RulesetComponentRequirement> requiredComponents;
    std::vector<RulesetRelationRequirement> requiredRelations;
    std::vector<MechanismDefinitionId> requiredDefinitions;
    std::vector<EntityDefinitionId> requiredEntityDefinitions;
    std::vector<RelationDefinitionId> requiredRelationDefinitions;
    std::vector<MechanismSpawnDefinitionId> requiredMechanismSpawns;
    std::vector<RulesetAlgorithmRequirement> requiredAlgorithms;
    std::vector<CapabilityRequirement> requiredCapabilities;
};

struct AppliedRulesetExtension
{
    RulesetId id;
    std::string canonicalName;
    std::uint32_t version = 0;
    std::int32_t priority = 0;
};

struct RulesetDefinition : RulesetRequirementSet
{
    RulesetId id;
    std::string canonicalName;
    std::uint32_t version = 0;
    std::vector<AppliedRulesetExtension> appliedExtensions;
};

struct RulesetVersionRange
{
    std::uint32_t minimumInclusive = 1;
    std::optional<std::uint32_t> maximumExclusive;

    bool IsValid() const noexcept;
    bool Contains(std::uint32_t version) const noexcept;
};

bool IsValidRulesetDefinition(const RulesetDefinition& ruleset);

enum class RulesetRegisterResult
{
    Added,
    InvalidRuleset,
    DuplicateVersion,
    IdCollision,
    Frozen
};

class RulesetRegistry
{
public:
    RulesetRegisterResult Register(RulesetDefinition ruleset);
    void Clear();
    void Freeze();
    bool IsFrozen() const noexcept;
    std::size_t Size() const noexcept;
    const RulesetDefinition* Find(
        RulesetId ruleset,
        std::uint32_t version
    ) const;
    const std::vector<RulesetDefinition>& All() const noexcept;

private:
    void RebuildIndex();

    std::vector<RulesetDefinition> rulesets_;
    std::map<std::pair<std::uint64_t, std::uint32_t>, std::size_t>
        indexByVersion_;
    bool frozen_ = false;
};

}
