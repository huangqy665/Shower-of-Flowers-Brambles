#include "authoring_pipeline.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <utility>

#include "authoring_parser.hpp"
#include "package_content_digest.hpp"

namespace dillen::authoring {

namespace {

inline constexpr parser::TemplateId kMechanismTemplate =
    0x44494C4C454E3001ULL;
inline constexpr parser::TemplateId kAlgorithmTemplate =
    0x44494C4C454E3002ULL;
inline constexpr parser::TemplateId kDefinitionTemplate =
    0x44494C4C454E3003ULL;
inline constexpr parser::TemplateId kSpawnTemplate =
    0x44494C4C454E3004ULL;
inline constexpr parser::TemplateId kRulesetTemplate =
    0x44494C4C454E3005ULL;
inline constexpr parser::TemplateId kComponentTemplate =
    0x44494C4C454E3006ULL;
inline constexpr parser::TemplateId kEntityTemplate =
    0x44494C4C454E3007ULL;
inline constexpr parser::TemplateId kRelationSchemaTemplate =
    0x44494C4C454E3008ULL;
inline constexpr parser::TemplateId kRelationDefinitionTemplate =
    0x44494C4C454E3009ULL;
inline constexpr parser::TemplateId kPackageTemplate =
    0x44494C4C454E300AULL;
inline constexpr parser::TemplateId kCapabilityTemplate =
    0x44494C4C454E300BULL;

inline constexpr parser::ResolutionPassId kDeclarePass =
    0x44494C4C454E4001ULL;
inline constexpr parser::ResolutionPassId kResolvePass =
    0x44494C4C454E4002ULL;
inline constexpr parser::ResolutionPassId kCompilePass =
    0x44494C4C454E4003ULL;

template <typename Document>
const Document* GetDocument(
    const parser::ParsedFile& file,
    parser::DefinitionTypeId type,
    parser::DiagnosticBag& diagnostics,
    std::string_view label
)
{
    if (file.result.artifact.type != type)
    {
        return nullptr;
    }
    const Document* document = file.result.artifact.As<Document>();
    if (document == nullptr)
    {
        diagnostics.Error(
            "dillen.authoring.artifact_type_mismatch",
            std::string(label) + " parser returned an invalid artifact"
        );
    }
    return document;
}

void ReportRegisterFailure(
    parser::DiagnosticBag& diagnostics,
    std::string code,
    std::string label,
    const parser::ParsedFile& file,
    parser::SourceSpan span
)
{
    diagnostics.Error(
        std::move(code),
        std::move(label) + " could not be added to its Registry ("
            + std::string(file.source.VirtualPath()) + ")",
        span
    );
}

bool SameSelection(
    const SelectedRulesetVersion& selection,
    kernel::RulesetId id,
    std::string_view canonicalName,
    std::uint32_t version
)
{
    return selection.id == id
        && selection.canonicalName == canonicalName
        && selection.version == version;
}

bool PackageRoleAllows(
    kernel::PackageRole role,
    parser::DefinitionTypeId type
)
{
    if (type == kPackageManifestDocumentType)
    {
        return true;
    }
    switch (role)
    {
    case kernel::PackageRole::Unspecified:
        return true;
    case kernel::PackageRole::Contract:
        return type == kCapabilityContractDocumentType
            || type == kComponentSchemaDocumentType
            || type == kRelationSchemaDocumentType;
    case kernel::PackageRole::Mechanism:
        return type == kMechanismTemplateDocumentType
            || type == kAlgorithmDescriptorDocumentType;
    case kernel::PackageRole::Content:
        return type == kMechanismDefinitionDocumentType
            || type == kMechanismSpawnDocumentType
            || type == kRulesetDocumentType
            || type == kEntityDefinitionDocumentType
            || type == kRelationDefinitionDocumentType;
    case kernel::PackageRole::Presentation:
        return false;
    }
    return false;
}

}

AuthoringSession::AuthoringSession(AuthoringLaunchSelection selection)
    : selection_(std::move(selection))
{
}

bool AuthoringSession::Register(
    parser::TemplateRegistry& templates,
    parser::ParserRegistry& parsers,
    parser::Resolver& resolver
)
{
    const auto addTemplate = [&templates](
        parser::TemplateId id,
        std::string name,
        std::string pattern,
        parser::ParserId parserId)
    {
        parser::FileTemplate fileTemplate;
        fileTemplate.id = id;
        fileTemplate.name = std::move(name);
        fileTemplate.pattern = std::move(pattern);
        fileTemplate.parser = parserId;
        fileTemplate.dialect = kDillenAuthoringDialect;
        fileTemplate.priority = 1000;
        return templates.Register(std::move(fileTemplate));
    };
    if (!addTemplate(
            kMechanismTemplate,
            "dillen_mechanism_template",
            "mechanisms/**/*.dmechanism",
            kMechanismTemplateParser)
        || !addTemplate(
            kAlgorithmTemplate,
            "dillen_algorithm_descriptor",
            "algorithms/**/*.dalgorithm",
            kAlgorithmDescriptorParser)
        || !addTemplate(
            kDefinitionTemplate,
            "dillen_mechanism_definition",
            "definitions/**/*.ddefinition",
            kMechanismDefinitionParser)
        || !addTemplate(
            kSpawnTemplate,
            "dillen_mechanism_spawn",
            "spawns/**/*.dspawn",
            kMechanismSpawnParser)
        || !addTemplate(
            kRulesetTemplate,
            "dillen_ruleset",
            "rulesets/**/*.druleset",
            kRulesetParser)
        || !addTemplate(
            kComponentTemplate,
            "dillen_component_schema",
            "components/**/*.dcomponent",
            kComponentSchemaParser)
        || !addTemplate(
            kEntityTemplate,
            "dillen_entity_definition",
            "entities/**/*.dentity",
            kEntityDefinitionParser)
        || !addTemplate(
            kRelationSchemaTemplate,
            "dillen_relation_schema",
            "relations/schemas/**/*.drelation",
            kRelationSchemaParser)
        || !addTemplate(
            kRelationDefinitionTemplate,
            "dillen_relation_definition",
            "relations/definitions/**/*.drelationdef",
            kRelationDefinitionParser)
        || !addTemplate(
            kPackageTemplate,
            "dillen_package_manifest",
            "packages/**/*.dpackage",
            kPackageManifestParser)
        || !addTemplate(
            kCapabilityTemplate,
            "dillen_capability_contract",
            "capabilities/**/*.dcapability",
            kCapabilityContractParser))
    {
        return false;
    }

    const auto addParser = [&parsers](
        parser::ParserId id,
        std::string name,
        parser::DefinitionTypeId outputType,
        parser::ParseFunction parse)
    {
        parser::ParserDescriptor descriptor;
        descriptor.id = id;
        descriptor.name = std::move(name);
        descriptor.inputDialect = kDillenAuthoringDialect;
        descriptor.outputType = outputType;
        descriptor.schemaVersion = 1;
        descriptor.parse = std::move(parse);
        return parsers.Register(std::move(descriptor));
    };
    if (!addParser(
            kMechanismTemplateParser,
            "dillen_mechanism_template",
            kMechanismTemplateDocumentType,
            ParseMechanismTemplate)
        || !addParser(
            kAlgorithmDescriptorParser,
            "dillen_algorithm_descriptor",
            kAlgorithmDescriptorDocumentType,
            ParseAlgorithmDescriptor)
        || !addParser(
            kMechanismDefinitionParser,
            "dillen_mechanism_definition",
            kMechanismDefinitionDocumentType,
            ParseMechanismDefinition)
        || !addParser(
            kMechanismSpawnParser,
            "dillen_mechanism_spawn",
            kMechanismSpawnDocumentType,
            ParseMechanismSpawn)
        || !addParser(
            kRulesetParser,
            "dillen_ruleset",
            kRulesetDocumentType,
            ParseRuleset)
        || !addParser(
            kComponentSchemaParser,
            "dillen_component_schema",
            kComponentSchemaDocumentType,
            ParseComponentSchema)
        || !addParser(
            kEntityDefinitionParser,
            "dillen_entity_definition",
            kEntityDefinitionDocumentType,
            ParseEntityDefinition)
        || !addParser(
            kRelationSchemaParser,
            "dillen_relation_schema",
            kRelationSchemaDocumentType,
            ParseRelationSchema)
        || !addParser(
            kRelationDefinitionParser,
            "dillen_relation_definition",
            kRelationDefinitionDocumentType,
            ParseRelationDefinition)
        || !addParser(
            kPackageManifestParser,
            "dillen_package_manifest",
            kPackageManifestDocumentType,
            ParsePackageManifest)
        || !addParser(
            kCapabilityContractParser,
            "dillen_capability_contract",
            kCapabilityContractDocumentType,
            ParseCapabilityContract))
    {
        return false;
    }

    parser::ResolutionPassDescriptor declare;
    declare.id = kDeclarePass;
    declare.name = "dillen_authoring_declare";
    declare.phase = parser::ResolutionPhase::Declare;
    declare.priority = 0;
    declare.run = [this](
        parser::ParseWorkspace& workspace,
        parser::DiagnosticBag& diagnostics)
    {
        return Declare(workspace, diagnostics);
    };
    parser::ResolutionPassDescriptor resolve;
    resolve.id = kResolvePass;
    resolve.name = "dillen_authoring_resolve";
    resolve.phase = parser::ResolutionPhase::Resolve;
    resolve.priority = 0;
    resolve.run = [this](
        parser::ParseWorkspace& workspace,
        parser::DiagnosticBag& diagnostics)
    {
        return Resolve(workspace, diagnostics);
    };
    parser::ResolutionPassDescriptor compile;
    compile.id = kCompilePass;
    compile.name = "dillen_authoring_compile";
    compile.phase = parser::ResolutionPhase::Validate;
    compile.priority = 0;
    compile.run = [this](
        parser::ParseWorkspace& workspace,
        parser::DiagnosticBag& diagnostics)
    {
        return ValidateAndCompile(workspace, diagnostics);
    };
    return resolver.RegisterPass(std::move(declare))
        && resolver.RegisterPass(std::move(resolve))
        && resolver.RegisterPass(std::move(compile));
}

bool AuthoringSession::Declare(
    parser::ParseWorkspace& workspace,
    parser::DiagnosticBag& diagnostics
)
{
    rootRulesets_.clear();
    extensionRulesets_.clear();
    for (const parser::ParsedFile& file : workspace.files)
    {
        if (!file.result.success
            || file.catalog.disposition
                != parser::CatalogDisposition::Active)
        {
            continue;
        }
        if (const MechanismTemplateDocument* document =
            GetDocument<MechanismTemplateDocument>(
                file,
                kMechanismTemplateDocumentType,
                diagnostics,
                "Mechanism Template"))
        {
            if (mechanismSchemas_.Register(document->value)
                != kernel::MechanismSchemaRegisterResult::Added)
            {
                ReportRegisterFailure(
                    diagnostics,
                    "dillen.authoring.mechanism_schema_rejected",
                    "Mechanism Template",
                    file,
                    document->declarationSpan
                );
            }
            continue;
        }
        if (const AlgorithmDescriptorDocument* document =
            GetDocument<AlgorithmDescriptorDocument>(
                file,
                kAlgorithmDescriptorDocumentType,
                diagnostics,
                "Algorithm Descriptor"))
        {
            if (algorithms_.Register(document->value)
                != kernel::AlgorithmRegisterResult::Added)
            {
                ReportRegisterFailure(
                    diagnostics,
                    "dillen.authoring.algorithm_rejected",
                    "Algorithm Descriptor",
                    file,
                    document->declarationSpan
                );
            }
            continue;
        }
        if (const RulesetDocument* document = GetDocument<RulesetDocument>(
                file,
                kRulesetDocumentType,
                diagnostics,
                "Ruleset"))
        {
            if (const auto* root = std::get_if<
                    kernel::RootRulesetDefinition>(&document->value))
            {
                rootRulesets_.push_back(*root);
            }
            else
            {
                extensionRulesets_.push_back(std::get<
                    kernel::ExtensionRulesetDefinition>(document->value));
            }
            continue;
        }
        if (const ComponentSchemaDocument* document =
            GetDocument<ComponentSchemaDocument>(
                file,
                kComponentSchemaDocumentType,
                diagnostics,
                "Component Schema"))
        {
            if (componentSchemas_.Register(document->value)
                != kernel::ComponentSchemaRegisterResult::Added)
            {
                ReportRegisterFailure(
                    diagnostics,
                    "dillen.authoring.component_schema_rejected",
                    "Component Schema",
                    file,
                    document->declarationSpan
                );
            }
            continue;
        }
        if (const RelationSchemaDocument* document =
            GetDocument<RelationSchemaDocument>(
                file,
                kRelationSchemaDocumentType,
                diagnostics,
                "Relation Schema"))
        {
            if (relationSchemas_.Register(document->value)
                != kernel::RelationSchemaRegisterResult::Added)
            {
                ReportRegisterFailure(
                    diagnostics,
                    "dillen.authoring.relation_schema_rejected",
                    "Relation Schema",
                    file,
                    document->declarationSpan
                );
            }
            continue;
        }
        if (const PackageManifestDocument* document =
            GetDocument<PackageManifestDocument>(
                file,
                kPackageManifestDocumentType,
                diagnostics,
                "Package Manifest"))
        {
            if (packageManifests_.Register(document->value)
                != kernel::PackageManifestRegisterResult::Added)
            {
                ReportRegisterFailure(
                    diagnostics,
                    "dillen.authoring.package_manifest_rejected",
                    "Package Manifest",
                    file,
                    document->declarationSpan
                );
            }
            continue;
        }
        if (const CapabilityContractDocument* document =
            GetDocument<CapabilityContractDocument>(
                file,
                kCapabilityContractDocumentType,
                diagnostics,
                "Capability Contract"))
        {
            if (capabilityContracts_.Register(document->value)
                != kernel::CapabilityContractRegisterResult::Added)
            {
                ReportRegisterFailure(
                    diagnostics,
                    "dillen.authoring.capability_contract_rejected",
                    "Capability Contract",
                    file,
                    document->declarationSpan
                );
            }
        }
    }
    if (diagnostics.HasErrors())
    {
        return false;
    }
    mechanismSchemas_.Freeze();
    componentSchemas_.Freeze();
    relationSchemas_.Freeze();
    algorithms_.Freeze();
    capabilityContracts_.Freeze();
    packageManifests_.Freeze();
    return true;
}

bool AuthoringSession::Resolve(
    parser::ParseWorkspace& workspace,
    parser::DiagnosticBag& diagnostics
)
{
    for (const parser::ParsedFile& file : workspace.files)
    {
        if (!file.result.success
            || file.catalog.disposition
                != parser::CatalogDisposition::Active)
        {
            continue;
        }
        const MechanismDefinitionDocument* document =
            GetDocument<MechanismDefinitionDocument>(
                file,
                kMechanismDefinitionDocumentType,
                diagnostics,
                "Mechanism Definition"
            );
        if (document != nullptr)
        {
            kernel::MechanismDefinition definition = document->value;
            definition.source.sourceName = file.catalog.sourceLayerName;
            definition.source.virtualPath = std::string(
                file.source.VirtualPath()
            );
            definition.source.line = document->declarationSpan.begin.line;
            definition.source.column = document->declarationSpan.begin.column;
            if (mechanismDefinitions_.Declare(
                    std::move(definition),
                    mechanismSchemas_,
                    algorithms_)
                != kernel::MechanismDefinitionDeclareResult::Added)
            {
                ReportRegisterFailure(
                    diagnostics,
                    "dillen.authoring.definition_rejected",
                    "Mechanism Definition",
                    file,
                    document->declarationSpan
                );
            }
            continue;
        }
        const EntityDefinitionDocument* entityDocument =
            GetDocument<EntityDefinitionDocument>(
                file,
                kEntityDefinitionDocumentType,
                diagnostics,
                "Entity Definition"
            );
        if (entityDocument == nullptr)
        {
            continue;
        }
        kernel::EntityDefinition entity = entityDocument->value;
        entity.source.sourceName = file.catalog.sourceLayerName;
        entity.source.virtualPath = std::string(file.source.VirtualPath());
        entity.source.line = entityDocument->declarationSpan.begin.line;
        entity.source.column = entityDocument->declarationSpan.begin.column;
        if (entityDefinitions_.Declare(
                std::move(entity),
                componentSchemas_)
            != kernel::EntityDefinitionDeclareResult::Added)
        {
            ReportRegisterFailure(
                diagnostics,
                "dillen.authoring.entity_definition_rejected",
                "Entity Definition",
                file,
                entityDocument->declarationSpan
            );
        }
    }
    if (diagnostics.HasErrors())
    {
        return false;
    }
    mechanismDefinitions_.Freeze();
    entityDefinitions_.Freeze();

    for (const parser::ParsedFile& file : workspace.files)
    {
        if (!file.result.success
            || file.catalog.disposition
                != parser::CatalogDisposition::Active)
        {
            continue;
        }
        const MechanismSpawnDocument* document =
            GetDocument<MechanismSpawnDocument>(
                file,
                kMechanismSpawnDocumentType,
                diagnostics,
                "Mechanism Spawn"
            );
        if (document != nullptr)
        {
            kernel::MechanismSpawnDefinition spawn = document->value;
            spawn.source.sourceName = file.catalog.sourceLayerName;
            spawn.source.virtualPath = std::string(file.source.VirtualPath());
            spawn.source.line = document->declarationSpan.begin.line;
            spawn.source.column = document->declarationSpan.begin.column;
            if (mechanismSpawns_.Declare(
                    std::move(spawn),
                    mechanismDefinitions_,
                    mechanismSchemas_)
                != kernel::MechanismSpawnDeclareResult::Added)
            {
                ReportRegisterFailure(
                    diagnostics,
                    "dillen.authoring.spawn_rejected",
                    "Mechanism Spawn",
                    file,
                    document->declarationSpan
                );
            }
            continue;
        }
        const RelationDefinitionDocument* relationDocument =
            GetDocument<RelationDefinitionDocument>(
                file,
                kRelationDefinitionDocumentType,
                diagnostics,
                "Relation Definition"
            );
        if (relationDocument == nullptr)
        {
            continue;
        }
        kernel::RelationDefinition relation = relationDocument->value;
        relation.origin.sourceName = file.catalog.sourceLayerName;
        relation.origin.virtualPath = std::string(file.source.VirtualPath());
        relation.origin.line = relationDocument->declarationSpan.begin.line;
        relation.origin.column = relationDocument->declarationSpan.begin.column;
        if (relationDefinitions_.Declare(
                std::move(relation),
                relationSchemas_,
                entityDefinitions_)
            != kernel::RelationDefinitionDeclareResult::Added)
        {
            ReportRegisterFailure(
                diagnostics,
                "dillen.authoring.relation_definition_rejected",
                "Relation Definition",
                file,
                relationDocument->declarationSpan
            );
        }
    }
    if (diagnostics.HasErrors())
    {
        return false;
    }
    mechanismSpawns_.Freeze();
    relationDefinitions_.Freeze();

    const auto root = std::find_if(
        rootRulesets_.begin(),
        rootRulesets_.end(),
        [this](const kernel::RootRulesetDefinition& candidate)
        {
            return SameSelection(
                selection_.root,
                candidate.ruleset.id,
                candidate.ruleset.canonicalName,
                candidate.ruleset.version
            );
        }
    );
    if (root == rootRulesets_.end())
    {
        diagnostics.Error(
            "dillen.authoring.root_ruleset_missing",
            "selected Root Ruleset was not found in external authoring sources"
        );
        return false;
    }

    std::set<kernel::RulesetId> selectedIds;
    std::vector<kernel::ExtensionRulesetDefinition> selectedExtensions;
    for (const SelectedRulesetVersion& selection : selection_.extensions)
    {
        if (!selection.id
            || selection.version == 0
            || selection.id
                != kernel::StableRulesetId(selection.canonicalName)
            || !selectedIds.emplace(selection.id).second)
        {
            diagnostics.Error(
                "dillen.authoring.extension_selection_invalid",
                "Extension Ruleset selection is invalid or duplicated"
            );
            continue;
        }
        const auto extension = std::find_if(
            extensionRulesets_.begin(),
            extensionRulesets_.end(),
            [&selection](
                const kernel::ExtensionRulesetDefinition& candidate)
            {
                return SameSelection(
                    selection,
                    candidate.id,
                    candidate.canonicalName,
                    candidate.version
                );
            }
        );
        if (extension == extensionRulesets_.end())
        {
            diagnostics.Error(
                "dillen.authoring.extension_ruleset_missing",
                "selected Extension Ruleset was not found: "
                    + selection.canonicalName
            );
            continue;
        }
        selectedExtensions.push_back(*extension);
    }
    if (diagnostics.HasErrors())
    {
        return false;
    }

    kernel::RulesetCompositionReport compositionReport;
    if (!kernel::RulesetComposer{}.Compose(
            *root,
            std::move(selectedExtensions),
            composedRuleset_,
            compositionReport))
    {
        for (const kernel::RulesetCompositionIssue& issue
            : compositionReport.issues)
        {
            diagnostics.Error(
                "dillen.authoring.ruleset_composition_failed",
                issue.subject + ": " + issue.message
            );
        }
        return false;
    }
    composed_ = true;
    if (rulesets_.Register(composedRuleset_)
        != kernel::RulesetRegisterResult::Added)
    {
        diagnostics.Error(
            "dillen.authoring.ruleset_registry_rejected",
            "composed Ruleset could not be registered"
        );
        return false;
    }
    rulesets_.Freeze();
    return true;
}

bool AuthoringSession::ValidateAndCompile(
    parser::ParseWorkspace& workspace,
    parser::DiagnosticBag& diagnostics
)
{
    if (!composed_)
    {
        diagnostics.Fatal(
            "dillen.authoring.ruleset_not_composed",
            "Runtime compilation requires a composed Ruleset"
        );
        return false;
    }
    struct SourceLayerBinding
    {
        const kernel::PackageManifest* manifest = nullptr;
        std::vector<const parser::ParsedFile*> files;
    };
    std::map<std::string, SourceLayerBinding> sourceLayers;
    for (const parser::ParsedFile& file : workspace.files)
    {
        if (!file.result.success
            || file.catalog.disposition
                != parser::CatalogDisposition::Active)
        {
            continue;
        }
        SourceLayerBinding& layer = sourceLayers[
            file.catalog.sourceLayerName
        ];
        layer.files.push_back(&file);
        if (file.result.artifact.type != kPackageManifestDocumentType)
        {
            continue;
        }
        const PackageManifestDocument* document =
            file.result.artifact.As<PackageManifestDocument>();
        if (document == nullptr || layer.manifest != nullptr)
        {
            diagnostics.Error(
                "dillen.authoring.package_source_ambiguous",
                "each Source Layer must contain exactly one Package Manifest: "
                    + file.catalog.sourceLayerName
            );
            continue;
        }
        layer.manifest = &document->value;
    }
    for (const auto& layerEntry : sourceLayers)
    {
        const std::string& layerName = layerEntry.first;
        const SourceLayerBinding& layer = layerEntry.second;
        if (layer.manifest == nullptr)
        {
            diagnostics.Error(
                "dillen.authoring.package_source_missing",
                "Source Layer has no Package Manifest: " + layerName
            );
            continue;
        }
        if (selection_.requireExplicitPackageRoles
            && layer.manifest->role == kernel::PackageRole::Unspecified)
        {
            diagnostics.Error(
                "dillen.authoring.package_role_required",
                "Source Layer Package must declare an explicit role: "
                    + layerName
            );
            continue;
        }
        for (const parser::ParsedFile* file : layer.files)
        {
            if (!PackageRoleAllows(
                    layer.manifest->role,
                    file->result.artifact.type))
            {
                diagnostics.Error(
                    "dillen.authoring.package_role_violation",
                    "Package role '"
                        + std::string(kernel::ToString(layer.manifest->role))
                        + "' cannot own source "
                        + file->catalog.virtualPath
                );
            }
        }
        std::vector<kernel::PackageContentSource> contentSources;
        for (const parser::ParsedFile* file : layer.files)
        {
            if (file->result.artifact.type
                == kPackageManifestDocumentType)
            {
                continue;
            }
            contentSources.push_back({
                file->catalog.virtualPath,
                file->source.Bytes()
            });
        }
        const std::string actualDigest =
            kernel::ComputePackageContentDigest(std::move(contentSources));
        if (actualDigest != layer.manifest->contentDigest)
        {
            diagnostics.Error(
                "dillen.authoring.package_content_digest_mismatch",
                "Package content_digest does not match Source Layer "
                    + layerName + "; expected " + actualDigest
            );
        }
    }
    std::map<kernel::PackageId, kernel::PackageRole> packageRoles;
    for (const auto& layerEntry : sourceLayers)
    {
        const kernel::PackageManifest* manifest = layerEntry.second.manifest;
        if (manifest != nullptr)
        {
            packageRoles.emplace(manifest->id, manifest->role);
        }
    }
    for (const auto& layerEntry : sourceLayers)
    {
        const kernel::PackageManifest* manifest = layerEntry.second.manifest;
        if (manifest == nullptr
            || manifest->role != kernel::PackageRole::Mechanism)
        {
            continue;
        }
        for (const kernel::PackageDependency& dependency
            : manifest->dependencies)
        {
            const auto target = packageRoles.find(dependency.package);
            if (target != packageRoles.end()
                && target->second != kernel::PackageRole::Unspecified
                && target->second != kernel::PackageRole::Contract)
            {
                diagnostics.Error(
                    "dillen.authoring.package_dependency_role_violation",
                    "Mechanism Package '" + manifest->canonicalName
                        + "' may depend only on Contract Packages; '"
                        + dependency.canonicalName + "' has role '"
                        + std::string(kernel::ToString(target->second)) + "'"
                );
            }
        }
    }
    if (diagnostics.HasErrors())
    {
        return false;
    }

    kernel::PackageLockReport packageReport;
    if (!kernel::PackageLockBuilder{}.Resolve(
            packageManifests_,
            composedRuleset_,
            packageLock_,
            packageReport))
    {
        for (const kernel::PackageLockIssue& issue : packageReport.issues)
        {
            diagnostics.Error(
                "dillen.authoring.package_lock_failed",
                issue.message
            );
        }
        return false;
    }
    std::vector<kernel::SourceLockEntry> sourceEntries;
    sourceEntries.reserve(workspace.files.size());
    for (const auto& layerEntry : sourceLayers)
    {
        const std::string& layerName = layerEntry.first;
        const SourceLayerBinding& layer = layerEntry.second;
        const kernel::PackageLockEntry* locked = packageLock_.Find(
            layer.manifest->id
        );
        if (locked == nullptr
            || locked->version != layer.manifest->version
            || locked->contentDigest != layer.manifest->contentDigest)
        {
            diagnostics.Error(
                "dillen.authoring.package_source_not_selected",
                "Source Layer Package is absent from the resolved Package Lock: "
                    + layerName
            );
            continue;
        }
        for (const parser::ParsedFile* file : layer.files)
        {
            sourceEntries.push_back({
                layer.manifest->id,
                layer.manifest->version,
                file->catalog.sourceLayerName,
                file->catalog.virtualPath,
                file->catalog.fingerprint,
                static_cast<std::uint64_t>(file->catalog.size)
            });
        }
    }
    if (diagnostics.HasErrors())
    {
        return false;
    }
    std::string sourceLockMessage;
    if (!kernel::SourceLockBuilder{}.Build(
            std::move(sourceEntries),
            sourceLock_,
            sourceLockMessage))
    {
        diagnostics.Error(
            "dillen.authoring.source_lock_failed",
            sourceLockMessage
        );
        return false;
    }
    if (!kernel::RuntimeCompiler{}.Compile(
            composedRuleset_,
            packageLock_,
            sourceLock_,
            mechanismSchemas_,
            componentSchemas_,
            relationSchemas_,
            algorithms_,
            mechanismDefinitions_,
            entityDefinitions_,
            relationDefinitions_,
            mechanismSpawns_,
            capabilityContracts_,
            runtimeCatalog_,
            compileReport_))
    {
        for (const kernel::RuntimeCompileIssue& issue
            : compileReport_.issues)
        {
            diagnostics.Error(
                "dillen.authoring.runtime_compile_failed",
                issue.subject + ": " + issue.message
            );
        }
        for (const kernel::RulesetIntegrityIssue& issue
            : compileReport_.integrity.issues)
        {
            diagnostics.Error(
                "dillen.authoring.integrity_failed",
                issue.subject + ": " + issue.message
            );
        }
        return false;
    }
    return true;
}

const kernel::MechanismSchemaRegistry&
AuthoringSession::MechanismSchemas() const noexcept
{
    return mechanismSchemas_;
}

const kernel::AlgorithmRegistry&
AuthoringSession::Algorithms() const noexcept
{
    return algorithms_;
}

const kernel::MechanismDefinitionRegistry&
AuthoringSession::MechanismDefinitions() const noexcept
{
    return mechanismDefinitions_;
}

const kernel::MechanismSpawnDefinitionRegistry&
AuthoringSession::MechanismSpawns() const noexcept
{
    return mechanismSpawns_;
}

const kernel::RelationSchemaRegistry&
AuthoringSession::RelationSchemas() const noexcept
{
    return relationSchemas_;
}

const kernel::RelationDefinitionRegistry&
AuthoringSession::RelationDefinitions() const noexcept
{
    return relationDefinitions_;
}

const kernel::RulesetRegistry& AuthoringSession::Rulesets() const noexcept
{
    return rulesets_;
}

const kernel::RulesetDefinition*
AuthoringSession::ComposedRuleset() const noexcept
{
    return composed_ ? &composedRuleset_ : nullptr;
}

const kernel::FrozenRuntimeCatalog&
AuthoringSession::RuntimeCatalog() const noexcept
{
    return runtimeCatalog_;
}

const kernel::PackageLock&
AuthoringSession::LockedPackages() const noexcept
{
    return packageLock_;
}

const kernel::SourceLock&
AuthoringSession::LockedSources() const noexcept
{
    return sourceLock_;
}

const kernel::RuntimeCompileReport&
AuthoringSession::CompileReport() const noexcept
{
    return compileReport_;
}

}
