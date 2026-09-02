#include <filesystem>
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
inline constexpr parser::TemplateId kSpawnTableTemplate =
    0x44494C4C454E0FF4ULL;
inline constexpr parser::TemplateId kPresentationAssetTemplate =
    0x44494C4C454E0FF3ULL;
inline constexpr parser::TemplateId kEntityTableTemplate =
    0x44494C4C454E0FF1ULL;
inline constexpr parser::TemplateId kRelationTableTemplate =
    0x44494C4C454E0FF2ULL;
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

// Checks what a Presentation Asset claims about the Ruleset.
//
// The asset's `kind` stays opaque -- nothing here knows what a panel is -- but
// its requirements are typed, and a claim like "this reads production_site's
// level" is answerable against the frozen catalog without knowing anything
// about user interfaces.
//
// Memo section 4.4.4 asks for exactly this: a Binding pointing at a Contract
// that does not exist must be refused at load time. Unchecked, the failure is
// far worse than a refusal -- the Package loads, the widget shows nothing, and
// the author has no way to tell which of their bindings is broken.
bool ValidatePresentationRequirements(
    const std::vector<kernel::PresentationAsset>& assets,
    const kernel::FrozenRuntimeCatalog& catalog,
    parser::DiagnosticBag& diagnostics
)
{
    bool ok = true;
    for (const kernel::PresentationAsset& asset : assets)
    {
        for (const kernel::PresentationAssetRequirement& requirement
            : asset.requirements)
        {
            bool resolved = false;
            std::string what;
            switch (requirement.kind)
            {
            case kernel::PresentationAssetRequirement::Kind::MechanismField:
            {
                const kernel::MechanismDefinitionId definition =
                    kernel::StableMechanismDefinitionId(
                        kernel::StableMechanismTypeId(requirement.primaryName),
                        requirement.secondaryName
                    );
                resolved = catalog.FindDefinition(definition) != nullptr
                    && catalog.ResolveDefinitionFieldSlot(
                        definition,
                        requirement.fieldName).has_value();
                what = "mechanism field " + requirement.primaryName + "/"
                    + requirement.secondaryName + "." + requirement.fieldName;
                break;
            }
            case kernel::PresentationAssetRequirement::Kind::ComponentField:
                resolved = catalog.ResolveComponentFieldSlot(
                    kernel::StableComponentTypeId(requirement.primaryName),
                    requirement.version,
                    requirement.fieldName).has_value();
                what = "component field " + requirement.primaryName + "."
                    + requirement.fieldName;
                break;
            case kernel::PresentationAssetRequirement::Kind::Capability:
                resolved = catalog.FindCapability(
                    kernel::StableCapabilityId(requirement.primaryName),
                    requirement.version) != nullptr;
                what = "capability " + requirement.primaryName;
                break;
            }
            if (!resolved)
            {
                diagnostics.Error(
                    "dillen.authoring.presentation_binding_unresolved",
                    "Presentation Asset '" + asset.canonicalName
                        + "' requires " + what
                        + ", which this Ruleset does not provide"
                );
                ok = false;
            }
        }
    }
    return ok;
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
            || type == kRelationDefinitionDocumentType
            // The bulk forms are Content, exactly like the single forms they
            // collapse. They declare no new kind of thing.
            || type == kEntityTableDocumentType
            || type == kRelationTableDocumentType
            || type == kSpawnTableDocumentType;
    case kernel::PackageRole::Presentation:
        // Presentation assets, and nothing else.
        //
        // A skin declares what it brings; it never declares a Mechanism, a
        // Definition or a Ruleset. That is what keeps it outside the
        // determinism closure by construction rather than by policy -- there
        // is no artifact it can own that the Package Lock would want.
        //
        // The asset form arrived with the map data it describes, not before
        // it. Inventing a raster or layout format against an imagined world
        // would have repeated the mistake this project has ruled against twice
        // (cancel_event by sequence, SetRole): syntax that can only be used
        // wrong is worse than a visible gap. The same rule governs every
        // future kind -- fonts, layouts, palettes -- each lands with the thing
        // it is meant to describe.
        return type == kPresentationAssetDocumentType;
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
            kEntityTableTemplate,
            "dillen_entity_table",
            "entities/**/*.dentitytable",
            kEntityTableParser)
        || !addTemplate(
            kRelationTableTemplate,
            "dillen_relation_table",
            "relations/definitions/**/*.drelationtable",
            kRelationTableParser)
        || !addTemplate(
            kPresentationAssetTemplate,
            "dillen_presentation_asset",
            "assets/**/*.dasset",
            kPresentationAssetParser)
        || !addTemplate(
            kSpawnTableTemplate,
            "dillen_spawn_table",
            "spawns/**/*.dspawntable",
            kSpawnTableParser)
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
            kEntityTableParser,
            "dillen_entity_table",
            kEntityTableDocumentType,
            ParseEntityTable)
        || !addParser(
            kRelationTableParser,
            "dillen_relation_table",
            kRelationTableDocumentType,
            ParseRelationTable)
        || !addParser(
            kPresentationAssetParser,
            "dillen_presentation_asset",
            kPresentationAssetDocumentType,
            ParsePresentationAsset)
        || !addParser(
            kSpawnTableParser,
            "dillen_spawn_table",
            kSpawnTableDocumentType,
            ParseSpawnTable)
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
        // A table declares the same objects the single form does, one after
        // another. It gets its own branch rather than being expanded earlier
        // because the source attribution is per-file, and a table's rows all
        // share the file they came from.
        if (file.result.artifact.type == kEntityTableDocumentType)
        {
            const EntityTableDocument* table =
                GetDocument<EntityTableDocument>(
                    file,
                    kEntityTableDocumentType,
                    diagnostics,
                    "Entity Table"
                );
            if (table == nullptr)
            {
                continue;
            }
            for (const kernel::EntityDefinition& row : table->value)
            {
                kernel::EntityDefinition entity = row;
                entity.source.sourceName = file.catalog.sourceLayerName;
                entity.source.virtualPath =
                    std::string(file.source.VirtualPath());
                entity.source.line = table->declarationSpan.begin.line;
                entity.source.column = table->declarationSpan.begin.column;
                if (entityDefinitions_.Declare(
                        std::move(entity),
                        componentSchemas_)
                    != kernel::EntityDefinitionDeclareResult::Added)
                {
                    ReportRegisterFailure(
                        diagnostics,
                        "dillen.authoring.entity_definition_rejected",
                        "Entity Table row '" + row.canonicalName + "'",
                        file,
                        table->declarationSpan
                    );
                    break;
                }
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
        if (file.result.artifact.type == kSpawnTableDocumentType)
        {
            const SpawnTableDocument* table =
                GetDocument<SpawnTableDocument>(
                    file,
                    kSpawnTableDocumentType,
                    diagnostics,
                    "Spawn Table"
                );
            if (table == nullptr)
            {
                continue;
            }
            for (const kernel::MechanismSpawnDefinition& row : table->value)
            {
                kernel::MechanismSpawnDefinition spawn = row;
                spawn.source.sourceName = file.catalog.sourceLayerName;
                spawn.source.virtualPath =
                    std::string(file.source.VirtualPath());
                spawn.source.line = table->declarationSpan.begin.line;
                spawn.source.column = table->declarationSpan.begin.column;
                if (mechanismSpawns_.Declare(
                        std::move(spawn),
                        mechanismDefinitions_,
                        mechanismSchemas_)
                    != kernel::MechanismSpawnDeclareResult::Added)
                {
                    ReportRegisterFailure(
                        diagnostics,
                        "dillen.authoring.spawn_rejected",
                        "Spawn Table row '" + row.canonicalName + "'",
                        file,
                        table->declarationSpan
                    );
                    break;
                }
            }
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
        if (file.result.artifact.type == kRelationTableDocumentType)
        {
            const RelationTableDocument* table =
                GetDocument<RelationTableDocument>(
                    file,
                    kRelationTableDocumentType,
                    diagnostics,
                    "Relation Table"
                );
            if (table == nullptr)
            {
                continue;
            }
            for (const kernel::RelationDefinition& row : table->value)
            {
                kernel::RelationDefinition relation = row;
                relation.origin.sourceName = file.catalog.sourceLayerName;
                relation.origin.virtualPath =
                    std::string(file.source.VirtualPath());
                relation.origin.line = table->declarationSpan.begin.line;
                relation.origin.column = table->declarationSpan.begin.column;
                if (relationDefinitions_.Declare(
                        std::move(relation),
                        relationSchemas_,
                        entityDefinitions_)
                    != kernel::RelationDefinitionDeclareResult::Added)
                {
                    ReportRegisterFailure(
                        diagnostics,
                        "dillen.authoring.relation_definition_rejected",
                        "Relation Table row '" + row.canonicalName + "'",
                        file,
                        table->declarationSpan
                    );
                    break;
                }
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
            // A Mechanism Package may not name a concrete Entity Definition.
            //
            // The role check above is about file kinds; this is about what is
            // written inside them. A Mechanism Package declares dependencies
            // only on Contract Packages, and Contract Packages declare
            // schemas, not entities. So an Entity Definition name appearing in
            // a Mechanism algorithm is by construction a name the Package
            // never declared a dependency on -- a hidden coupling to whatever
            // Content happens to be loaded, and exactly the hard-coded game
            // element the layering exists to prevent.
            //
            // This is not hypothetical: the first computed set_component_field
            // written into this repository named `dillen.demo05.alvara`
            // directly, and nothing complained. Every gate passed. The
            // replaceable-Package promise was quietly false until this check
            // existed.
            //
            // The supported way to reach an Entity is a role slot, which
            // Content binds and the Mechanism only knows by name.
            if (layer.manifest->role != kernel::PackageRole::Mechanism)
            {
                continue;
            }
            const AlgorithmDescriptorDocument* algorithm =
                file->result.artifact.type == kAlgorithmDescriptorDocumentType
                ? file->result.artifact.As<AlgorithmDescriptorDocument>()
                : nullptr;
            if (algorithm == nullptr)
            {
                continue;
            }
            const auto reportEntityName = [&](const std::string& what)
            {
                diagnostics.Error(
                    "dillen.authoring.package_entity_reference_violation",
                    "Mechanism Package '" + layer.manifest->canonicalName
                        + "' names a concrete Entity Definition in "
                        + file->catalog.virtualPath + " (" + what
                        + "); a Mechanism Package may only reach Entities "
                          "through a role slot"
                );
            };
            const auto inspect =
                [&](const kernel::AlgorithmInstructionDefinition& instruction)
            {
                switch (instruction.kind)
                {
                case kernel::AlgorithmInstructionKind::CreateEntity:
                    reportEntityName("create_entity");
                    break;
                case kernel::AlgorithmInstructionKind::SetComponentField:
                case kernel::AlgorithmInstructionKind::
                    SetComponentFieldComputed:
                    reportEntityName("set_component_field owner");
                    break;
                case kernel::AlgorithmInstructionKind::AddRelation:
                    reportEntityName("add_relation endpoint");
                    break;
                default:
                    break;
                }
            };
            for (const auto& stage : algorithm->value.program.stages)
            {
                for (const kernel::AlgorithmInstructionDefinition& instruction
                    : stage.second)
                {
                    inspect(instruction);
                }
            }
            for (const auto& stage : algorithm->value.script.stages)
            {
                for (const kernel::ControlledScriptInstructionDefinition&
                    instruction : stage.second)
                {
                    if (instruction.kind
                        == kernel::ControlledScriptInstructionKind::Transact)
                    {
                        inspect(instruction.action);
                    }
                }
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
        if (manifest == nullptr)
        {
            continue;
        }
        if (manifest->role == kernel::PackageRole::Mechanism)
        {
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
                            + std::string(kernel::ToString(target->second))
                            + "'"
                    );
                }
            }
        }
        // Symmetric half: a Presentation Package may not declare
        // dependencies either.
        //
        // Being outside the closure means its dependencies would never be
        // resolved -- nothing requires it, so nothing walks its graph. A
        // declaration that silently does nothing is worse than a refusal: a
        // skin author writes `dependency = { name = ... required = yes }`,
        // sees it accepted, and believes the version range is being enforced.
        if (manifest->role == kernel::PackageRole::Presentation
            && !manifest->dependencies.empty())
        {
            diagnostics.Error(
                "dillen.authoring.presentation_package_not_authoritative",
                "Presentation Package '" + manifest->canonicalName
                    + "' declares dependencies; it is outside the determinism "
                      "closure, so nothing would ever resolve them"
            );
        }
        // Nothing authoritative may depend on a Presentation Package.
        //
        // The Package Lock is resolved from the Ruleset's package
        // requirements plus their dependency closure, and every Lock entry --
        // id, version, content_digest, load index -- is hashed into the
        // Ruleset Fingerprint, which in turn is what a save validates against.
        // So a Presentation Package that reached the closure by either route
        // would put the map skin inside the save identity: change the skin and
        // existing saves stop loading, and two clients with different skins
        // would compute different fingerprints for the same simulation.
        //
        // Presentation is loaded on the host side, outside the closure, and
        // that is what makes it deletable at the content level -- the same
        // property src/CMakeLists.txt gives it at the module level.
        for (const kernel::PackageDependency& dependency
            : manifest->dependencies)
        {
            const auto target = packageRoles.find(dependency.package);
            if (target != packageRoles.end()
                && target->second == kernel::PackageRole::Presentation)
            {
                diagnostics.Error(
                    "dillen.authoring.presentation_package_not_authoritative",
                    "Package '" + manifest->canonicalName
                        + "' depends on Presentation Package '"
                        + dependency.canonicalName
                        + "'; Presentation Packages are outside the "
                          "determinism closure and cannot be depended upon"
                );
            }
        }
    }
    // The same rule from the Ruleset side. A Ruleset requirement is the other
    // way into the Package Lock, and it does not go through any manifest's
    // dependency list.
    for (const kernel::RulesetPackageRequirement& requirement
        : composedRuleset_.packages)
    {
        const auto role = packageRoles.find(requirement.package);
        if (role != packageRoles.end()
            && role->second == kernel::PackageRole::Presentation)
        {
            diagnostics.Error(
                "dillen.authoring.presentation_package_not_authoritative",
                "Ruleset requires Presentation Package '"
                    + requirement.canonicalName
                    + "'; Presentation Packages are outside the determinism "
                      "closure and cannot enter the Package Lock"
            );
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
        // A Presentation Package is loaded, parsed and integrity-checked like
        // any other, and then stops here.
        //
        // It is deliberately absent from the Package Lock -- nothing
        // authoritative may require or depend on one -- so demanding Lock
        // membership would make a Presentation Package unloadable, and the
        // role would be decorative. Skipping the Source Lock as well is the
        // point rather than a shortcut: every Source Lock entry is hashed into
        // the Ruleset Fingerprint, so contributing one would put the map skin
        // inside the save identity and change the skin would stop existing
        // saves loading.
        //
        // What it keeps: its manifest is still validated, its content_digest
        // is still verified against the files on disk, and PackageRoleAllows
        // still governs what it may own. Tampering with a skin is a cosmetic
        // problem, not a determinism one, so it is reported without entering
        // the closure.
        if (layer.manifest->role == kernel::PackageRole::Presentation)
        {
            // Collected here rather than in the Declare pass, because this is
            // the one place that already knows a layer is Presentation and is
            // about to exclude it from everything the simulation is sealed
            // with. The assets still need somewhere to go.
            for (const parser::ParsedFile* file : layer.files)
            {
                if (file->result.artifact.type
                    != kPresentationAssetDocumentType)
                {
                    continue;
                }
                const PresentationAssetDocument* asset =
                    file->result.artifact.As<PresentationAssetDocument>();
                if (asset == nullptr)
                {
                    continue;
                }
                kernel::PresentationAsset declared = asset->value;
                declared.source.sourceName = file->catalog.sourceLayerName;
                declared.source.virtualPath =
                    std::string(file->source.VirtualPath());
                // The payload resolves against the directory of the source
                // that declared it, so a Package stays relocatable and an
                // asset can never reach outside the Package that owns it.
                declared.source.physicalDirectory =
                    std::filesystem::path(file->catalog.physicalPath)
                        .parent_path()
                        .string();
                bool duplicate = false;
                for (const kernel::PresentationAsset& existing
                    : presentationAssets_)
                {
                    duplicate = duplicate
                        || existing.canonicalName == declared.canonicalName;
                }
                if (duplicate)
                {
                    diagnostics.Error(
                        "dillen.authoring.presentation_asset_duplicate",
                        "Presentation Asset '" + declared.canonicalName
                            + "' is declared more than once"
                    );
                    continue;
                }
                presentationAssets_.push_back(std::move(declared));
            }
            continue;
        }
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
    presentationFingerprint_ =
        kernel::ComputePresentationFingerprint(presentationAssets_);

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
    // After the catalog is frozen, because that is the first moment the
    // question can be answered. A Binding claims something about the Ruleset;
    // the Ruleset does not exist until here.
    if (!ValidatePresentationRequirements(
            presentationAssets_,
            runtimeCatalog_,
            diagnostics))
    {
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

const std::vector<kernel::PresentationAsset>&
AuthoringSession::PresentationAssets() const noexcept
{
    return presentationAssets_;
}

kernel::PresentationFingerprint
AuthoringSession::PresentationFingerprint() const noexcept
{
    return presentationFingerprint_;
}

}
