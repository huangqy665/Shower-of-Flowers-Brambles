#include "ruleset_composition.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace dillen::kernel {

namespace {

void AddIssue(
    RulesetCompositionReport& report,
    RulesetCompositionIssueCode code,
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

RulesetDefinition AsRulesetDefinition(
    const ExtensionRulesetDefinition& extension
)
{
    RulesetDefinition definition;
    static_cast<RulesetRequirementSet&>(definition) = extension;
    definition.id = extension.id;
    definition.canonicalName = extension.canonicalName;
    definition.version = extension.version;
    return definition;
}

bool IsValidContractKey(RulesetContractKey key)
{
    if (key.id == 0)
    {
        return false;
    }
    switch (key.kind)
    {
    case RulesetContractKind::MechanismSchema:
    case RulesetContractKind::ComponentSchema:
    case RulesetContractKind::RelationSchema:
    case RulesetContractKind::Algorithm:
        return true;
    case RulesetContractKind::Package:
    case RulesetContractKind::MechanismDefinition:
    case RulesetContractKind::EntityDefinition:
    case RulesetContractKind::RelationDefinition:
    case RulesetContractKind::MechanismSpawn:
    case RulesetContractKind::Capability:
        return key.version == 0;
    }
    return false;
}

bool IsAllowed(
    const RulesetExtensionPolicy& policy,
    RulesetContractKind kind
)
{
    return std::find(
        policy.allowedAdditions.begin(),
        policy.allowedAdditions.end(),
        kind
    ) != policy.allowedAdditions.end();
}

bool MatchesProtected(
    RulesetContractKey protection,
    RulesetContractKey addition
)
{
    return protection.kind == addition.kind
        && protection.id == addition.id
        && (protection.version == 0
            || protection.version == addition.version);
}

std::vector<RulesetContractKey> ContractKeys(
    const RulesetRequirementSet& requirements
)
{
    std::vector<RulesetContractKey> keys;
    keys.reserve(
        requirements.packages.size()
        + requirements.requiredSchemas.size()
        + requirements.requiredComponents.size()
        + requirements.requiredRelations.size()
        + requirements.requiredDefinitions.size()
        + requirements.requiredEntityDefinitions.size()
        + requirements.requiredRelationDefinitions.size()
        + requirements.requiredMechanismSpawns.size()
        + requirements.requiredAlgorithms.size()
        + requirements.requiredCapabilities.size()
    );
    for (const RulesetPackageRequirement& requirement
        : requirements.packages)
    {
        keys.push_back({
            RulesetContractKind::Package,
            requirement.package.value,
            0
        });
    }
    for (const RulesetSchemaRequirement& requirement
        : requirements.requiredSchemas)
    {
        keys.push_back({
            RulesetContractKind::MechanismSchema,
            requirement.type.value,
            requirement.version
        });
    }
    for (const RulesetComponentRequirement& requirement
        : requirements.requiredComponents)
    {
        keys.push_back({
            RulesetContractKind::ComponentSchema,
            requirement.type.value,
            requirement.version
        });
    }
    for (const RulesetRelationRequirement& requirement
        : requirements.requiredRelations)
    {
        keys.push_back({
            RulesetContractKind::RelationSchema,
            requirement.type.value,
            requirement.version
        });
    }
    for (MechanismDefinitionId definition
        : requirements.requiredDefinitions)
    {
        keys.push_back({
            RulesetContractKind::MechanismDefinition,
            definition.value,
            0
        });
    }
    for (EntityDefinitionId definition
        : requirements.requiredEntityDefinitions)
    {
        keys.push_back({
            RulesetContractKind::EntityDefinition,
            definition.value,
            0
        });
    }
    for (RelationDefinitionId definition
        : requirements.requiredRelationDefinitions)
    {
        keys.push_back({
            RulesetContractKind::RelationDefinition,
            definition.value,
            0
        });
    }
    for (MechanismSpawnDefinitionId spawn
        : requirements.requiredMechanismSpawns)
    {
        keys.push_back({
            RulesetContractKind::MechanismSpawn,
            spawn.value,
            0
        });
    }
    for (const RulesetAlgorithmRequirement& requirement
        : requirements.requiredAlgorithms)
    {
        keys.push_back({
            RulesetContractKind::Algorithm,
            requirement.algorithm.value,
            requirement.version
        });
    }
    for (const CapabilityRequirement& requirement
        : requirements.requiredCapabilities)
    {
        keys.push_back({
            RulesetContractKind::Capability,
            requirement.capability.value,
            0
        });
    }
    return keys;
}

void AppendRequirements(
    RulesetDefinition& output,
    const ExtensionRulesetDefinition& extension
)
{
    output.packages.insert(
        output.packages.end(),
        extension.packages.begin(),
        extension.packages.end()
    );
    output.requiredSchemas.insert(
        output.requiredSchemas.end(),
        extension.requiredSchemas.begin(),
        extension.requiredSchemas.end()
    );
    output.requiredComponents.insert(
        output.requiredComponents.end(),
        extension.requiredComponents.begin(),
        extension.requiredComponents.end()
    );
    output.requiredRelations.insert(
        output.requiredRelations.end(),
        extension.requiredRelations.begin(),
        extension.requiredRelations.end()
    );
    output.requiredDefinitions.insert(
        output.requiredDefinitions.end(),
        extension.requiredDefinitions.begin(),
        extension.requiredDefinitions.end()
    );
    output.requiredEntityDefinitions.insert(
        output.requiredEntityDefinitions.end(),
        extension.requiredEntityDefinitions.begin(),
        extension.requiredEntityDefinitions.end()
    );
    output.requiredRelationDefinitions.insert(
        output.requiredRelationDefinitions.end(),
        extension.requiredRelationDefinitions.begin(),
        extension.requiredRelationDefinitions.end()
    );
    output.requiredMechanismSpawns.insert(
        output.requiredMechanismSpawns.end(),
        extension.requiredMechanismSpawns.begin(),
        extension.requiredMechanismSpawns.end()
    );
    output.requiredAlgorithms.insert(
        output.requiredAlgorithms.end(),
        extension.requiredAlgorithms.begin(),
        extension.requiredAlgorithms.end()
    );
    output.requiredCapabilities.insert(
        output.requiredCapabilities.end(),
        extension.requiredCapabilities.begin(),
        extension.requiredCapabilities.end()
    );
}

void SortRequirements(RulesetDefinition& ruleset)
{
    std::sort(
        ruleset.packages.begin(),
        ruleset.packages.end(),
        [](const RulesetPackageRequirement& first,
           const RulesetPackageRequirement& second)
        {
            return first.package < second.package;
        }
    );
    std::sort(
        ruleset.requiredSchemas.begin(),
        ruleset.requiredSchemas.end(),
        [](const RulesetSchemaRequirement& first,
           const RulesetSchemaRequirement& second)
        {
            return first.type != second.type
                ? first.type < second.type
                : first.version < second.version;
        }
    );
    std::sort(
        ruleset.requiredComponents.begin(),
        ruleset.requiredComponents.end(),
        [](const RulesetComponentRequirement& first,
           const RulesetComponentRequirement& second)
        {
            return first.type != second.type
                ? first.type < second.type
                : first.version < second.version;
        }
    );
    std::sort(
        ruleset.requiredRelations.begin(),
        ruleset.requiredRelations.end(),
        [](const RulesetRelationRequirement& first,
           const RulesetRelationRequirement& second)
        {
            return first.type != second.type
                ? first.type < second.type
                : first.version < second.version;
        }
    );
    std::sort(
        ruleset.requiredDefinitions.begin(),
        ruleset.requiredDefinitions.end()
    );
    std::sort(
        ruleset.requiredEntityDefinitions.begin(),
        ruleset.requiredEntityDefinitions.end()
    );
    std::sort(
        ruleset.requiredRelationDefinitions.begin(),
        ruleset.requiredRelationDefinitions.end()
    );
    std::sort(
        ruleset.requiredMechanismSpawns.begin(),
        ruleset.requiredMechanismSpawns.end()
    );
    std::sort(
        ruleset.requiredAlgorithms.begin(),
        ruleset.requiredAlgorithms.end(),
        [](const RulesetAlgorithmRequirement& first,
           const RulesetAlgorithmRequirement& second)
        {
            return first.algorithm != second.algorithm
                ? first.algorithm < second.algorithm
                : first.version < second.version;
        }
    );
    std::sort(
        ruleset.requiredCapabilities.begin(),
        ruleset.requiredCapabilities.end(),
        [](const CapabilityRequirement& first,
           const CapabilityRequirement& second)
        {
            return first.capability < second.capability;
        }
    );
}

}

bool operator==(
    RulesetContractKey first,
    RulesetContractKey second
) noexcept
{
    return first.kind == second.kind
        && first.id == second.id
        && first.version == second.version;
}

bool operator<(
    RulesetContractKey first,
    RulesetContractKey second
) noexcept
{
    if (first.kind != second.kind)
    {
        return first.kind < second.kind;
    }
    if (first.id != second.id)
    {
        return first.id < second.id;
    }
    return first.version < second.version;
}

bool RulesetCompositionReport::Success() const noexcept
{
    return issues.empty();
}

bool IsValidRootRulesetDefinition(
    const RootRulesetDefinition& root
)
{
    if (!IsValidRulesetDefinition(root.ruleset)
        || !root.ruleset.appliedExtensions.empty())
    {
        return false;
    }
    std::set<RulesetContractKind> allowed;
    for (RulesetContractKind kind
        : root.extensionPolicy.allowedAdditions)
    {
        if (!allowed.emplace(kind).second)
        {
            return false;
        }
    }
    std::set<RulesetContractKey> protectedContracts;
    for (RulesetContractKey key
        : root.extensionPolicy.protectedContracts)
    {
        if (!IsValidContractKey(key)
            || !protectedContracts.emplace(key).second)
        {
            return false;
        }
    }
    return true;
}

bool IsValidExtensionRulesetDefinition(
    const ExtensionRulesetDefinition& extension
)
{
    if (!extension.targetRoot
        || !extension.targetVersions.IsValid()
        || !IsValidMechanismSymbol(extension.targetRootCanonicalName)
        || extension.targetRootCanonicalName
            != NormalizeMechanismSymbol(
                extension.targetRootCanonicalName)
        || extension.targetRoot
            != StableRulesetId(extension.targetRootCanonicalName))
    {
        return false;
    }
    return IsValidRulesetDefinition(AsRulesetDefinition(extension));
}

bool RulesetComposer::Compose(
    const RootRulesetDefinition& root,
    std::vector<ExtensionRulesetDefinition> extensions,
    RulesetDefinition& output,
    RulesetCompositionReport& report
) const
{
    output = {};
    report = {};
    if (!IsValidRootRulesetDefinition(root))
    {
        AddIssue(
            report,
            IsValidRulesetDefinition(root.ruleset)
                ? RulesetCompositionIssueCode::InvalidPolicy
                : RulesetCompositionIssueCode::InvalidRoot,
            root.ruleset.canonicalName,
            "Root Ruleset or its extension policy is invalid"
        );
        return false;
    }

    std::sort(
        extensions.begin(),
        extensions.end(),
        [](const ExtensionRulesetDefinition& first,
           const ExtensionRulesetDefinition& second)
        {
            if (first.priority != second.priority)
            {
                return first.priority < second.priority;
            }
            if (first.id != second.id)
            {
                return first.id < second.id;
            }
            return first.version < second.version;
        }
    );

    std::set<RulesetId> selectedExtensions;
    for (const ExtensionRulesetDefinition& extension : extensions)
    {
        if (!IsValidExtensionRulesetDefinition(extension))
        {
            AddIssue(
                report,
                RulesetCompositionIssueCode::InvalidExtension,
                extension.canonicalName,
                "Extension Ruleset is structurally invalid"
            );
            continue;
        }
        if (!selectedExtensions.emplace(extension.id).second)
        {
            AddIssue(
                report,
                RulesetCompositionIssueCode::DuplicateExtension,
                extension.canonicalName,
                "Only one version of an Extension Ruleset may be selected"
            );
        }
        if (extension.targetRoot != root.ruleset.id
            || extension.targetRootCanonicalName
                != root.ruleset.canonicalName)
        {
            AddIssue(
                report,
                RulesetCompositionIssueCode::TargetRootMismatch,
                extension.canonicalName,
                "Extension Ruleset targets a different Root Ruleset"
            );
        }
        else if (!extension.targetVersions.Contains(root.ruleset.version))
        {
            AddIssue(
                report,
                RulesetCompositionIssueCode::TargetVersionMismatch,
                extension.canonicalName,
                "Extension Ruleset does not support the selected Root version"
            );
        }
    }
    if (!report.Success())
    {
        return false;
    }

    const std::vector<RulesetContractKey> rootKeys = ContractKeys(
        root.ruleset
    );
    std::set<RulesetContractKey> occupied(
        rootKeys.begin(),
        rootKeys.end()
    );
    for (const ExtensionRulesetDefinition& extension : extensions)
    {
        const std::vector<RulesetContractKey> additions = ContractKeys(
            extension
        );
        for (RulesetContractKey addition : additions)
        {
            if (!IsAllowed(root.extensionPolicy, addition.kind))
            {
                AddIssue(
                    report,
                    RulesetCompositionIssueCode::AdditionForbidden,
                    extension.canonicalName,
                    "Root Ruleset does not permit this contract category"
                );
                continue;
            }
            const bool explicitlyProtected = std::any_of(
                root.extensionPolicy.protectedContracts.begin(),
                root.extensionPolicy.protectedContracts.end(),
                [addition](RulesetContractKey protection)
                {
                    return MatchesProtected(protection, addition);
                }
            );
            const bool ownedByRoot = std::find(
                rootKeys.begin(),
                rootKeys.end(),
                addition
            ) != rootKeys.end();
            if (explicitlyProtected || ownedByRoot)
            {
                AddIssue(
                    report,
                    RulesetCompositionIssueCode::ProtectedContractCollision,
                    extension.canonicalName,
                    "Extension collides with a Root-protected contract"
                );
            }
            else if (!occupied.emplace(addition).second)
            {
                AddIssue(
                    report,
                    RulesetCompositionIssueCode::ContractCollision,
                    extension.canonicalName,
                    "Multiple extensions add the same contract"
                );
            }
        }
    }
    if (!report.Success())
    {
        return false;
    }

    output = root.ruleset;
    for (const ExtensionRulesetDefinition& extension : extensions)
    {
        AppendRequirements(output, extension);
        AppliedRulesetExtension applied;
        applied.id = extension.id;
        applied.canonicalName = extension.canonicalName;
        applied.version = extension.version;
        applied.priority = extension.priority;
        output.appliedExtensions.push_back(applied);
        report.appliedExtensions.push_back(std::move(applied));
    }
    SortRequirements(output);
    if (!IsValidRulesetDefinition(output))
    {
        output = {};
        report.appliedExtensions.clear();
        AddIssue(
            report,
            RulesetCompositionIssueCode::ContractCollision,
            root.ruleset.canonicalName,
            "Composed Ruleset failed structural validation"
        );
        return false;
    }
    return true;
}

}
