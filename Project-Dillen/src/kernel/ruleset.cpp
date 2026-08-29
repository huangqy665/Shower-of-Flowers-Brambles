#include "ruleset.hpp"

#include <algorithm>
#include <set>
#include <unordered_set>
#include <utility>

namespace dillen::kernel {

namespace {

bool ValidateRulesetStructure(const RulesetDefinition& ruleset)
{
    if (!ruleset.id
        || ruleset.version == 0
        || !IsValidMechanismSymbol(ruleset.canonicalName)
        || ruleset.canonicalName
            != NormalizeMechanismSymbol(ruleset.canonicalName)
        || ruleset.id != StableRulesetId(ruleset.canonicalName))
    {
        return false;
    }

    std::set<PackageId> packages;
    for (const RulesetPackageRequirement& requirement : ruleset.packages)
    {
        if (!requirement.package
            || !requirement.versions.IsValid()
            || !IsValidMechanismSymbol(requirement.canonicalName)
            || requirement.canonicalName
                != NormalizeMechanismSymbol(requirement.canonicalName)
            || requirement.package
                != StablePackageId(requirement.canonicalName)
            || !packages.emplace(requirement.package).second)
        {
            return false;
        }
    }

    std::set<std::pair<std::uint64_t, std::uint32_t>> schemas;
    for (const RulesetSchemaRequirement& requirement
        : ruleset.requiredSchemas)
    {
        if (!requirement.type
            || requirement.version == 0
            || !schemas.emplace(
                requirement.type.value,
                requirement.version).second)
        {
            return false;
        }
    }
    std::set<MechanismDefinitionId> definitions;
    for (MechanismDefinitionId definition : ruleset.requiredDefinitions)
    {
        if (!definition || !definitions.emplace(definition).second)
        {
            return false;
        }
    }
    std::set<std::pair<std::uint64_t, std::uint32_t>> components;
    for (const RulesetComponentRequirement& requirement
        : ruleset.requiredComponents)
    {
        if (!requirement.type
            || requirement.version == 0
            || !components.emplace(
                requirement.type.value,
                requirement.version).second)
        {
            return false;
        }
    }
    std::set<std::pair<std::uint64_t, std::uint32_t>> relations;
    for (const RulesetRelationRequirement& requirement
        : ruleset.requiredRelations)
    {
        if (!requirement.type
            || requirement.version == 0
            || !relations.emplace(
                requirement.type.value,
                requirement.version).second)
        {
            return false;
        }
    }
    std::set<EntityDefinitionId> entityDefinitions;
    for (EntityDefinitionId definition
        : ruleset.requiredEntityDefinitions)
    {
        if (!definition || !entityDefinitions.emplace(definition).second)
        {
            return false;
        }
    }
    std::set<RelationDefinitionId> relationDefinitions;
    for (RelationDefinitionId definition
        : ruleset.requiredRelationDefinitions)
    {
        if (!definition
            || !relationDefinitions.emplace(definition).second)
        {
            return false;
        }
    }
    std::set<MechanismSpawnDefinitionId> mechanismSpawns;
    for (MechanismSpawnDefinitionId spawn
        : ruleset.requiredMechanismSpawns)
    {
        if (!spawn || !mechanismSpawns.emplace(spawn).second)
        {
            return false;
        }
    }
    std::set<std::pair<std::uint64_t, std::uint32_t>> algorithms;
    for (const RulesetAlgorithmRequirement& requirement
        : ruleset.requiredAlgorithms)
    {
        if (!requirement.algorithm
            || requirement.version == 0
            || !algorithms.emplace(
                requirement.algorithm.value,
                requirement.version).second)
        {
            return false;
        }
    }
    std::unordered_set<std::uint64_t> capabilities;
    for (const CapabilityRequirement& capability
        : ruleset.requiredCapabilities)
    {
        if (!IsValidCapabilityRequirement(capability)
            || !capabilities.emplace(capability.capability.value).second)
        {
            return false;
        }
    }
    std::set<RulesetId> extensions;
    for (const AppliedRulesetExtension& extension
        : ruleset.appliedExtensions)
    {
        if (!extension.id
            || extension.version == 0
            || !IsValidMechanismSymbol(extension.canonicalName)
            || extension.canonicalName
                != NormalizeMechanismSymbol(extension.canonicalName)
            || extension.id
                != StableRulesetId(extension.canonicalName)
            || !extensions.emplace(extension.id).second)
        {
            return false;
        }
    }
    return true;
}

}

bool RulesetVersionRange::IsValid() const noexcept
{
    return minimumInclusive > 0
        && (!maximumExclusive.has_value()
            || minimumInclusive < *maximumExclusive);
}

bool RulesetVersionRange::Contains(std::uint32_t version) const noexcept
{
    return IsValid()
        && version >= minimumInclusive
        && (!maximumExclusive.has_value()
            || version < *maximumExclusive);
}

bool IsValidRulesetDefinition(const RulesetDefinition& ruleset)
{
    return ValidateRulesetStructure(ruleset);
}

RulesetRegisterResult RulesetRegistry::Register(RulesetDefinition ruleset)
{
    if (frozen_)
    {
        return RulesetRegisterResult::Frozen;
    }
    if (!IsValidRulesetDefinition(ruleset))
    {
        return RulesetRegisterResult::InvalidRuleset;
    }
    const auto key = std::make_pair(ruleset.id.value, ruleset.version);
    if (indexByVersion_.find(key) != indexByVersion_.end())
    {
        return RulesetRegisterResult::DuplicateVersion;
    }
    for (const RulesetDefinition& existing : rulesets_)
    {
        if (existing.id == ruleset.id
            && existing.canonicalName != ruleset.canonicalName)
        {
            return RulesetRegisterResult::IdCollision;
        }
    }
    indexByVersion_[key] = rulesets_.size();
    rulesets_.push_back(std::move(ruleset));
    return RulesetRegisterResult::Added;
}

void RulesetRegistry::Clear()
{
    if (frozen_)
    {
        return;
    }
    rulesets_.clear();
    indexByVersion_.clear();
}

void RulesetRegistry::Freeze()
{
    if (frozen_)
    {
        return;
    }
    std::sort(
        rulesets_.begin(),
        rulesets_.end(),
        [](const RulesetDefinition& first, const RulesetDefinition& second)
        {
            if (first.id != second.id)
            {
                return first.id < second.id;
            }
            return first.version < second.version;
        }
    );
    RebuildIndex();
    frozen_ = true;
}

bool RulesetRegistry::IsFrozen() const noexcept
{
    return frozen_;
}

std::size_t RulesetRegistry::Size() const noexcept
{
    return rulesets_.size();
}

const RulesetDefinition* RulesetRegistry::Find(
    RulesetId ruleset,
    std::uint32_t version
) const
{
    const auto iterator = indexByVersion_.find({ruleset.value, version});
    return iterator == indexByVersion_.end()
        ? nullptr
        : &rulesets_[iterator->second];
}

const std::vector<RulesetDefinition>& RulesetRegistry::All() const noexcept
{
    return rulesets_;
}

void RulesetRegistry::RebuildIndex()
{
    indexByVersion_.clear();
    for (std::size_t index = 0; index < rulesets_.size(); ++index)
    {
        indexByVersion_[{
            rulesets_[index].id.value,
            rulesets_[index].version
        }] = index;
    }
}

}
