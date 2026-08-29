#include <algorithm>
#include <iostream>
#include <vector>

#include "package_lock.hpp"
#include "ruleset_composition.hpp"
#include "ruleset_fingerprint.hpp"

using namespace dillen::kernel;

namespace {

RootRulesetDefinition MakeRoot(std::string name)
{
    RootRulesetDefinition root;
    root.ruleset.canonicalName = std::move(name);
    root.ruleset.id = StableRulesetId(root.ruleset.canonicalName);
    root.ruleset.version = 3;
    root.ruleset.requiredSchemas.push_back({
        StableMechanismTypeId("dillen.test.root_schema"),
        1
    });
    root.extensionPolicy.allowedAdditions = {
        RulesetContractKind::MechanismSchema,
        RulesetContractKind::MechanismDefinition,
        RulesetContractKind::Algorithm
    };
    root.extensionPolicy.protectedContracts.push_back({
        RulesetContractKind::MechanismDefinition,
        StableMechanismDefinitionId(
            StableMechanismTypeId("dillen.test.root_schema"),
            "dillen.test.reserved_definition"
        ).value,
        0
    });
    return root;
}

ExtensionRulesetDefinition MakeExtension(
    const RootRulesetDefinition& root,
    std::string name,
    std::int32_t priority
)
{
    ExtensionRulesetDefinition extension;
    extension.canonicalName = std::move(name);
    extension.id = StableRulesetId(extension.canonicalName);
    extension.version = 1;
    extension.priority = priority;
    extension.targetRoot = root.ruleset.id;
    extension.targetRootCanonicalName = root.ruleset.canonicalName;
    extension.targetVersions = {3, 4};
    return extension;
}

bool HasIssue(
    const RulesetCompositionReport& report,
    RulesetCompositionIssueCode code
)
{
    return std::any_of(
        report.issues.begin(),
        report.issues.end(),
        [code](const RulesetCompositionIssue& issue)
        {
            return issue.code == code;
        }
    );
}

bool ResolveEmptyLock(PackageLock& packageLock)
{
    PackageManifestRegistry manifests;
    manifests.Freeze();
    PackageLockReport report;
    RulesetDefinition emptyRuleset;
    emptyRuleset.canonicalName = "dillen.test.empty_lock";
    emptyRuleset.id = StableRulesetId(emptyRuleset.canonicalName);
    emptyRuleset.version = 1;
    return PackageLockBuilder{}.Resolve(
        manifests,
        emptyRuleset,
        packageLock,
        report
    );
}

bool SameComposedRequirements(
    const RulesetDefinition& first,
    const RulesetDefinition& second
)
{
    if (first.requiredSchemas.size() != second.requiredSchemas.size()
        || first.requiredDefinitions != second.requiredDefinitions
        || first.requiredAlgorithms.size()
            != second.requiredAlgorithms.size())
    {
        return false;
    }
    for (std::size_t index = 0;
        index < first.requiredSchemas.size();
        ++index)
    {
        if (first.requiredSchemas[index].type
                != second.requiredSchemas[index].type
            || first.requiredSchemas[index].version
                != second.requiredSchemas[index].version)
        {
            return false;
        }
    }
    for (std::size_t index = 0;
        index < first.requiredAlgorithms.size();
        ++index)
    {
        if (first.requiredAlgorithms[index].algorithm
                != second.requiredAlgorithms[index].algorithm
            || first.requiredAlgorithms[index].version
                != second.requiredAlgorithms[index].version)
        {
            return false;
        }
    }
    return true;
}

}

int main()
{
    const RootRulesetDefinition root = MakeRoot(
        "dillen.test.root_alpha"
    );
    ExtensionRulesetDefinition definitions = MakeExtension(
        root,
        "dillen.test.extension_definitions",
        20
    );
    const MechanismTypeId extensionType = StableMechanismTypeId(
        "dillen.test.extension_schema"
    );
    definitions.requiredSchemas.push_back({extensionType, 1});
    definitions.requiredDefinitions.push_back(
        StableMechanismDefinitionId(
            extensionType,
            "dillen.test.extension_definition"
        )
    );

    ExtensionRulesetDefinition algorithms = MakeExtension(
        root,
        "dillen.test.extension_algorithms",
        10
    );
    algorithms.requiredAlgorithms.push_back({
        StableAlgorithmId("dillen.test.extension_algorithm"),
        1
    });

    RulesetDefinition first;
    RulesetDefinition second;
    RulesetCompositionReport firstReport;
    RulesetCompositionReport secondReport;
    if (!RulesetComposer{}.Compose(
            root,
            {definitions, algorithms},
            first,
            firstReport)
        || !RulesetComposer{}.Compose(
            root,
            {algorithms, definitions},
            second,
            secondReport))
    {
        std::cerr << "Valid Root/Extension composition was rejected\n";
        return 1;
    }
    if (first.appliedExtensions.size() != 2
        || first.appliedExtensions[0].id != algorithms.id
        || first.appliedExtensions[1].id != definitions.id
        || !SameComposedRequirements(first, second))
    {
        std::cerr << "Composition order is not deterministic\n";
        return 1;
    }

    PackageLock packageLock;
    if (!ResolveEmptyLock(packageLock)
        || ComputeRulesetFingerprint(first, packageLock)
            != ComputeRulesetFingerprint(second, packageLock))
    {
        std::cerr << "Composition fingerprint is not deterministic\n";
        return 1;
    }

    RulesetDefinition algorithmOnly;
    RulesetDefinition renamedAlgorithmOnly;
    RulesetCompositionReport algorithmOnlyReport;
    RulesetCompositionReport renamedAlgorithmOnlyReport;
    ExtensionRulesetDefinition renamedAlgorithms = algorithms;
    renamedAlgorithms.canonicalName =
        "dillen.test.extension_algorithms_renamed";
    renamedAlgorithms.id = StableRulesetId(
        renamedAlgorithms.canonicalName
    );
    if (!RulesetComposer{}.Compose(
            root,
            {algorithms},
            algorithmOnly,
            algorithmOnlyReport)
        || !RulesetComposer{}.Compose(
            root,
            {renamedAlgorithms},
            renamedAlgorithmOnly,
            renamedAlgorithmOnlyReport)
        || ComputeRulesetFingerprint(algorithmOnly, packageLock)
            == ComputeRulesetFingerprint(
                renamedAlgorithmOnly,
                packageLock))
    {
        std::cerr << "Extension identity is absent from fingerprint\n";
        return 1;
    }

    const RootRulesetDefinition alternativeRoot = MakeRoot(
        "dillen.test.root_beta"
    );
    RulesetDefinition alternative;
    RulesetCompositionReport alternativeReport;
    if (!RulesetComposer{}.Compose(
            alternativeRoot,
            {},
            alternative,
            alternativeReport)
        || alternative.id == first.id)
    {
        std::cerr << "Root Ruleset is not independently replaceable\n";
        return 1;
    }

    ExtensionRulesetDefinition forbidden = MakeExtension(
        root,
        "dillen.test.extension_forbidden",
        30
    );
    forbidden.requiredComponents.push_back({
        StableComponentTypeId("dillen.test.forbidden_component"),
        1
    });
    RulesetDefinition rejected;
    RulesetCompositionReport rejectedReport;
    if (RulesetComposer{}.Compose(
            root,
            {forbidden},
            rejected,
            rejectedReport)
        || !HasIssue(
            rejectedReport,
            RulesetCompositionIssueCode::AdditionForbidden))
    {
        std::cerr << "Forbidden extension category was accepted\n";
        return 1;
    }

    ExtensionRulesetDefinition protectedCollision = MakeExtension(
        root,
        "dillen.test.extension_protected_collision",
        30
    );
    protectedCollision.requiredDefinitions.push_back(
        StableMechanismDefinitionId(
            StableMechanismTypeId("dillen.test.root_schema"),
            "dillen.test.reserved_definition"
        )
    );
    rejectedReport = {};
    if (RulesetComposer{}.Compose(
            root,
            {protectedCollision},
            rejected,
            rejectedReport)
        || !HasIssue(
            rejectedReport,
            RulesetCompositionIssueCode::ProtectedContractCollision))
    {
        std::cerr << "Protected Root contract was overridden\n";
        return 1;
    }

    ExtensionRulesetDefinition wrongRoot = definitions;
    wrongRoot.targetRootCanonicalName = alternativeRoot.ruleset.canonicalName;
    wrongRoot.targetRoot = alternativeRoot.ruleset.id;
    rejectedReport = {};
    if (RulesetComposer{}.Compose(
            root,
            {wrongRoot},
            rejected,
            rejectedReport)
        || !HasIssue(
            rejectedReport,
            RulesetCompositionIssueCode::TargetRootMismatch))
    {
        std::cerr << "Mismatched Root target was accepted\n";
        return 1;
    }

    ExtensionRulesetDefinition wrongVersion = definitions;
    wrongVersion.targetVersions = {4, 5};
    rejectedReport = {};
    if (RulesetComposer{}.Compose(
            root,
            {wrongVersion},
            rejected,
            rejectedReport)
        || !HasIssue(
            rejectedReport,
            RulesetCompositionIssueCode::TargetVersionMismatch))
    {
        std::cerr << "Mismatched Root version was accepted\n";
        return 1;
    }

    std::cout
        << "Root/Extension Ruleset composition: passed (fingerprint "
        << ComputeRulesetFingerprint(first, packageLock).ToHex()
        << ")\n";
    return 0;
}
