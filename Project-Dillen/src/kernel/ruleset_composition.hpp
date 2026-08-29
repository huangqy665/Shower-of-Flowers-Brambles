#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ruleset.hpp"

namespace dillen::kernel {

enum class RulesetContractKind : std::uint8_t
{
    Package,
    MechanismSchema,
    ComponentSchema,
    RelationSchema,
    MechanismDefinition,
    EntityDefinition,
    RelationDefinition,
    MechanismSpawn,
    Algorithm,
    Capability
};

struct RulesetContractKey
{
    RulesetContractKind kind = RulesetContractKind::Package;
    std::uint64_t id = 0;
    std::uint32_t version = 0;
};

bool operator==(
    RulesetContractKey first,
    RulesetContractKey second
) noexcept;
bool operator<(
    RulesetContractKey first,
    RulesetContractKey second
) noexcept;

struct RulesetExtensionPolicy
{
    std::vector<RulesetContractKind> allowedAdditions;
    std::vector<RulesetContractKey> protectedContracts;
};

struct RootRulesetDefinition
{
    RulesetDefinition ruleset;
    RulesetExtensionPolicy extensionPolicy;
};

struct ExtensionRulesetDefinition : RulesetRequirementSet
{
    RulesetId id;
    std::string canonicalName;
    std::uint32_t version = 0;
    std::int32_t priority = 0;
    RulesetId targetRoot;
    std::string targetRootCanonicalName;
    RulesetVersionRange targetVersions;
};

enum class RulesetCompositionIssueCode
{
    InvalidRoot,
    InvalidPolicy,
    InvalidExtension,
    DuplicateExtension,
    TargetRootMismatch,
    TargetVersionMismatch,
    AdditionForbidden,
    ProtectedContractCollision,
    ContractCollision
};

struct RulesetCompositionIssue
{
    RulesetCompositionIssueCode code =
        RulesetCompositionIssueCode::InvalidRoot;
    std::string subject;
    std::string message;
};

struct RulesetCompositionReport
{
    std::vector<RulesetCompositionIssue> issues;
    std::vector<AppliedRulesetExtension> appliedExtensions;

    bool Success() const noexcept;
};

bool IsValidRootRulesetDefinition(
    const RootRulesetDefinition& root
);
bool IsValidExtensionRulesetDefinition(
    const ExtensionRulesetDefinition& extension
);

class RulesetComposer
{
public:
    bool Compose(
        const RootRulesetDefinition& root,
        std::vector<ExtensionRulesetDefinition> extensions,
        RulesetDefinition& output,
        RulesetCompositionReport& report
    ) const;
};

}
