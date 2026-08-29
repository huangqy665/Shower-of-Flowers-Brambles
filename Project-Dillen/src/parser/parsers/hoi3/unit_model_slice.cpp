#include "unit_model_slice.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

#include "country_tag_slice.hpp"
#include "unit_model_parser.hpp"

namespace dillen::parser::hoi3 {

namespace {

std::optional<dillen::compatibility::hoi3::content::CountryTag> CountryTagFromPath(
    std::string_view virtualPath
)
{
    const std::size_t slash = virtualPath.find_last_of('/');
    const std::string_view filename = slash == std::string_view::npos
        ? virtualPath
        : virtualPath.substr(slash + 1);
    if (filename.size() < 4
        || (filename[3] != ' ' && filename[3] != '-'))
    {
        return std::nullopt;
    }
    return dillen::compatibility::hoi3::content::CountryTag::Parse(filename.substr(0, 3));
}

dillen::compatibility::hoi3::content::DefinitionOrigin MakeOrigin(
    const ParsedFile& file,
    const SourceSpan& span
)
{
    dillen::compatibility::hoi3::content::DefinitionOrigin origin;
    origin.virtualPath = std::string(file.source.VirtualPath());
    origin.sourceLayer = file.catalog.sourceLayerName;
    origin.line = span.IsValid() ? span.begin.line : 1;
    origin.column = span.IsValid() ? span.begin.column : 1;
    return origin;
}

bool SameTechnologyLevels(
    const std::vector<dillen::compatibility::hoi3::content::UnitModelTechnologyLevel>& first,
    const std::vector<dillen::compatibility::hoi3::content::UnitModelTechnologyLevel>& second
)
{
    if (first.size() != second.size())
    {
        return false;
    }
    for (std::size_t index = 0; index < first.size(); ++index)
    {
        if (first[index].name != second[index].name
            || first[index].level != second[index].level)
        {
            return false;
        }
    }
    return true;
}

bool DeclareUnitModels(
    ParseWorkspace& workspace,
    DiagnosticBag& diagnostics,
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions
)
{
    std::unordered_set<std::uint32_t> reportedCountries;
    for (const ParsedFile& file : workspace.files)
    {
        if (file.result.artifact.type != kUnitModelDocumentType)
        {
            continue;
        }
        const UnitModelDocument* document =
            file.result.artifact.As<UnitModelDocument>();
        if (document == nullptr)
        {
            diagnostics.Error(
                "hoi3.unit_model.artifact_type_mismatch",
                "Unit model parser returned an invalid artifact"
            );
            return false;
        }
        const auto tag = CountryTagFromPath(file.source.VirtualPath());
        if (!tag)
        {
            diagnostics.Error(
                "hoi3.unit_model.filename_tag_missing",
                "Unit model filename must begin with a Country Tag"
            );
            continue;
        }
        const dillen::compatibility::hoi3::content::CountryDefinitionId country = tag->StableId();
        if (definitions.Countries().Find(country) == nullptr
            && reportedCountries.emplace(country.value).second)
        {
            diagnostics.Warning(
                "hoi3.unit_model.country_unresolved",
                "Unit model Country Tag '" + tag->ToString()
                    + "' is absent from the active Country Registry"
            );
        }

        for (const UnresolvedUnitModelDefinition& unresolved
            : document->definitions)
        {
            const dillen::compatibility::hoi3::content::UnitModelDefinition* existing =
                definitions.UnitModels().Find(
                    country,
                    unresolved.unitTypeName,
                    unresolved.modelIndex
                );
            if (existing != nullptr)
            {
                if (SameTechnologyLevels(
                        existing->technologyLevels,
                        unresolved.technologyLevels))
                {
                    diagnostics.Warning(
                        "hoi3.unit_model.duplicate_identical_ignored",
                        "ignored an identical duplicate Unit model",
                        unresolved.span
                    );
                }
                else
                {
                    diagnostics.Error(
                        "hoi3.unit_model.duplicate_conflict",
                        "Unit model key has conflicting definitions",
                        unresolved.span
                    );
                }
                continue;
            }

            dillen::compatibility::hoi3::content::UnitModelDefinition definition;
            definition.id = dillen::compatibility::hoi3::content::StableUnitModelDefinitionId(
                country,
                unresolved.unitTypeName,
                unresolved.modelIndex
            );
            definition.country = country;
            definition.unitTypeName = unresolved.unitTypeName;
            definition.normalizedUnitTypeName =
                dillen::compatibility::hoi3::content::NormalizeUnitTypeName(unresolved.unitTypeName);
            definition.modelIndex = unresolved.modelIndex;
            definition.technologyLevels = unresolved.technologyLevels;
            definition.origin = MakeOrigin(file, unresolved.span);
            const dillen::compatibility::hoi3::content::UnitModelDeclareResult result =
                definitions.UnitModels().Declare(std::move(definition));
            if (result != dillen::compatibility::hoi3::content::UnitModelDeclareResult::Added)
            {
                diagnostics.Error(
                    "hoi3.unit_model.declare_failed",
                    "Unit model could not be declared",
                    unresolved.span
                );
            }
        }
    }
    return !diagnostics.HasErrors();
}

bool ResolveUnitModels(
    ParseWorkspace& workspace,
    DiagnosticBag& diagnostics,
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions
)
{
    std::unordered_set<std::uint64_t> resolvedModels;
    for (const ParsedFile& file : workspace.files)
    {
        if (file.result.artifact.type != kUnitModelDocumentType)
        {
            continue;
        }
        const UnitModelDocument* document =
            file.result.artifact.As<UnitModelDocument>();
        const auto tag = CountryTagFromPath(file.source.VirtualPath());
        if (document == nullptr || !tag)
        {
            return false;
        }
        const dillen::compatibility::hoi3::content::CountryDefinitionId country = tag->StableId();
        for (const UnresolvedUnitModelDefinition& unresolved
            : document->definitions)
        {
            const dillen::compatibility::hoi3::content::UnitModelDefinitionId id =
                dillen::compatibility::hoi3::content::StableUnitModelDefinitionId(
                    country,
                    unresolved.unitTypeName,
                    unresolved.modelIndex
                );
            if (!resolvedModels.emplace(id.value).second)
            {
                continue;
            }

            std::optional<dillen::compatibility::hoi3::content::UnitTypeDefinitionId> unitType;
            const dillen::compatibility::hoi3::content::UnitTypeDefinition* unit =
                definitions.UnitTypes().Find(unresolved.unitTypeName);
            if (unit != nullptr)
            {
                unitType = unit->id;
            }

            std::vector<dillen::compatibility::hoi3::content::UnitModelTechnologyLevel> levels =
                unresolved.technologyLevels;
            for (dillen::compatibility::hoi3::content::UnitModelTechnologyLevel& level : levels)
            {
                const dillen::compatibility::hoi3::content::TechnologyDefinition* technology =
                    definitions.Technologies().Find(level.name);
                if (technology != nullptr)
                {
                    level.technology = technology->id;
                }
            }
            const dillen::compatibility::hoi3::content::UnitModelResolveResult result =
                definitions.UnitModels().ResolveReferences(
                    id,
                    unitType,
                    std::move(levels)
                );
            if (result != dillen::compatibility::hoi3::content::UnitModelResolveResult::Resolved)
            {
                diagnostics.Error(
                    "hoi3.unit_model.resolve_failed",
                    "Unit model references could not be resolved",
                    unresolved.span
                );
            }
        }
    }
    return !diagnostics.HasErrors();
}

}

bool RegisterUnitModelSlice(
    TemplateRegistry& templates,
    ParserRegistry& parsers,
    Resolver& resolver,
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions
)
{
    FileTemplate fileTemplate;
    fileTemplate.id = kUnitModelTemplate;
    fileTemplate.name = "hoi3_unit_model";
    fileTemplate.pattern = "units/models/**/*.txt";
    fileTemplate.parser = kUnitModelParser;
    fileTemplate.dialect = kHoi3ClausewitzDialect;
    fileTemplate.priority = 2000;
    if (!templates.Register(std::move(fileTemplate)))
    {
        return false;
    }

    ParserDescriptor parser;
    parser.id = kUnitModelParser;
    parser.name = "hoi3_unit_model";
    parser.inputDialect = kHoi3ClausewitzDialect;
    parser.outputType = kUnitModelDocumentType;
    parser.schemaVersion = 1;
    parser.parse = ParseUnitModels;
    if (!parsers.Register(std::move(parser)))
    {
        templates.Unregister(kUnitModelTemplate);
        return false;
    }

    ResolutionPassDescriptor declarePass;
    declarePass.id = kUnitModelDeclarePass;
    declarePass.name = "hoi3_unit_model_declare";
    declarePass.phase = ResolutionPhase::Declare;
    declarePass.priority = 100;
    declarePass.run = [&definitions](
        ParseWorkspace& workspace,
        DiagnosticBag& diagnostics)
    {
        return DeclareUnitModels(workspace, diagnostics, definitions);
    };
    if (!resolver.RegisterPass(std::move(declarePass)))
    {
        parsers.Unregister(kUnitModelParser);
        templates.Unregister(kUnitModelTemplate);
        return false;
    }

    ResolutionPassDescriptor resolvePass;
    resolvePass.id = kUnitModelResolvePass;
    resolvePass.name = "hoi3_unit_model_resolve";
    resolvePass.phase = ResolutionPhase::Resolve;
    resolvePass.priority = -1400;
    resolvePass.run = [&definitions](
        ParseWorkspace& workspace,
        DiagnosticBag& diagnostics)
    {
        return ResolveUnitModels(workspace, diagnostics, definitions);
    };
    if (!resolver.RegisterPass(std::move(resolvePass)))
    {
        resolver.UnregisterPass(kUnitModelDeclarePass);
        parsers.Unregister(kUnitModelParser);
        templates.Unregister(kUnitModelTemplate);
        return false;
    }
    return true;
}

}
