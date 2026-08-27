#include "unit_type_slice.hpp"

#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "country_tag_slice.hpp"
#include "unit_type_parser.hpp"

namespace dillen::parser::hoi3 {

namespace {

bool IsUnitTypeSource(
    std::string_view virtualPath,
    std::string_view
)
{
    return virtualPath.rfind("units/models/", 0) != 0;
}

content::DefinitionOrigin MakeOrigin(
    const ParsedFile& file,
    const SourceSpan& span
)
{
    content::DefinitionOrigin origin;
    origin.virtualPath = std::string(file.source.VirtualPath());
    origin.sourceLayer = file.catalog.sourceLayerName;
    origin.line = span.IsValid() ? span.begin.line : 1;
    origin.column = span.IsValid() ? span.begin.column : 1;
    return origin;
}

bool DeclareUnitTypes(
    AnalysisWorkspace& workspace,
    DiagnosticBag& diagnostics,
    content::DefinitionRegistry& definitions
)
{
    for (const ParsedFile& file : workspace.files)
    {
        if (file.result.artifact.type != kUnitTypeDocumentType)
        {
            continue;
        }
        const UnitTypeDocument* document =
            file.result.artifact.As<UnitTypeDocument>();
        if (document == nullptr)
        {
            diagnostics.Error(
                "hoi3.unit_type.artifact_type_mismatch",
                "Unit type parser returned an invalid artifact"
            );
            return false;
        }
        for (const UnresolvedUnitTypeDefinition& unresolved
            : document->definitions)
        {
            content::UnitTypeDefinition definition;
            definition.id = content::StableUnitTypeDefinitionId(
                unresolved.name
            );
            definition.name = unresolved.name;
            definition.normalizedName = content::NormalizeUnitTypeName(
                unresolved.name
            );
            definition.domain = unresolved.domain;
            definition.sprite = unresolved.sprite;
            definition.active = unresolved.active;
            definition.unitGroup = unresolved.unitGroup;
            definition.scalarProperties = unresolved.scalarProperties;
            definition.modifierBlocks = unresolved.modifierBlocks;
            definition.origin = MakeOrigin(file, unresolved.span);
            const content::UnitTypeDeclareResult result =
                definitions.UnitTypes().Declare(std::move(definition));
            if (result == content::UnitTypeDeclareResult::Added)
            {
                continue;
            }
            const std::string message = result
                    == content::UnitTypeDeclareResult::DuplicateName
                ? "duplicate Unit type definition '" + unresolved.name + "'"
                : result == content::UnitTypeDeclareResult::IdCollision
                    ? "Unit type stable ID collision for '"
                        + unresolved.name + "'"
                    : "invalid Unit type definition '"
                        + unresolved.name + "'";
            diagnostics.Error(
                "hoi3.unit_type.declare_failed",
                message,
                unresolved.span
            );
        }
    }
    return !diagnostics.HasErrors();
}

bool ResolveUnitTypes(
    AnalysisWorkspace& workspace,
    DiagnosticBag& diagnostics,
    content::DefinitionRegistry& definitions
)
{
    std::unordered_set<std::uint32_t> reportedCountries;
    for (const ParsedFile& file : workspace.files)
    {
        if (file.result.artifact.type != kUnitTypeDocumentType)
        {
            continue;
        }
        const UnitTypeDocument* document =
            file.result.artifact.As<UnitTypeDocument>();
        if (document == nullptr)
        {
            return false;
        }
        for (const UnresolvedUnitTypeDefinition& unresolved
            : document->definitions)
        {
            std::vector<content::CountryDefinitionId> usableBy;
            std::unordered_set<std::uint32_t> seen;
            usableBy.reserve(unresolved.usableBy.size());
            for (const std::string& text : unresolved.usableBy)
            {
                const auto tag = content::CountryTag::Parse(text);
                if (!tag)
                {
                    diagnostics.Error(
                        "hoi3.unit_type.usable_by_resolve_invalid",
                        "Unit usable_by contains an invalid Country Tag",
                        unresolved.span
                    );
                    continue;
                }
                const content::CountryDefinitionId id = tag->StableId();
                if (definitions.Countries().Find(id) == nullptr
                    && reportedCountries.emplace(id.value).second)
                {
                    diagnostics.Warning(
                        "hoi3.unit_type.country_unresolved",
                        "Country Tag '" + tag->ToString()
                            + "' is absent from the active country index",
                        unresolved.span
                    );
                }
                if (seen.emplace(id.value).second)
                {
                    usableBy.push_back(id);
                }
                else
                {
                    diagnostics.Warning(
                        "hoi3.unit_type.usable_by_duplicate",
                        "ignored duplicate usable_by Country Tag '"
                            + tag->ToString() + "'",
                        unresolved.span
                    );
                }
            }
            const content::UnitTypeResolveResult result =
                definitions.UnitTypes().ResolveUsableBy(
                    content::StableUnitTypeDefinitionId(unresolved.name),
                    std::move(usableBy)
                );
            if (result != content::UnitTypeResolveResult::Resolved)
            {
                diagnostics.Error(
                    "hoi3.unit_type.resolve_failed",
                    "Unit usable_by references could not be resolved",
                    unresolved.span
                );
            }
        }
    }
    return !diagnostics.HasErrors();
}

}

bool RegisterUnitTypeSlice(
    TemplateRegistry& templates,
    ParserRegistry& parsers,
    Analyzer& analyzer,
    content::DefinitionRegistry& definitions
)
{
    FileTemplate fileTemplate;
    fileTemplate.id = kUnitTypeTemplate;
    fileTemplate.name = "hoi3_unit_type";
    fileTemplate.pattern = "units/**/*.txt";
    fileTemplate.parser = kUnitTypeParser;
    fileTemplate.dialect = kHoi3ClausewitzDialect;
    fileTemplate.priority = 1000;
    fileTemplate.probe = IsUnitTypeSource;
    if (!templates.Register(std::move(fileTemplate)))
    {
        return false;
    }

    ParserDescriptor parser;
    parser.id = kUnitTypeParser;
    parser.name = "hoi3_unit_type";
    parser.inputDialect = kHoi3ClausewitzDialect;
    parser.outputType = kUnitTypeDocumentType;
    parser.schemaVersion = 1;
    parser.parse = ParseUnitTypes;
    if (!parsers.Register(std::move(parser)))
    {
        templates.Unregister(kUnitTypeTemplate);
        return false;
    }

    AnalysisPassDescriptor declarePass;
    declarePass.id = kUnitTypeDeclarePass;
    declarePass.name = "hoi3_unit_type_declare";
    declarePass.phase = AnalysisPhase::Declare;
    declarePass.priority = 0;
    declarePass.run = [&definitions](
        AnalysisWorkspace& workspace,
        DiagnosticBag& diagnostics)
    {
        return DeclareUnitTypes(workspace, diagnostics, definitions);
    };
    if (!analyzer.RegisterPass(std::move(declarePass)))
    {
        parsers.Unregister(kUnitTypeParser);
        templates.Unregister(kUnitTypeTemplate);
        return false;
    }

    AnalysisPassDescriptor resolvePass;
    resolvePass.id = kUnitTypeResolvePass;
    resolvePass.name = "hoi3_unit_type_resolve";
    resolvePass.phase = AnalysisPhase::Resolve;
    resolvePass.priority = -1600;
    resolvePass.run = [&definitions](
        AnalysisWorkspace& workspace,
        DiagnosticBag& diagnostics)
    {
        return ResolveUnitTypes(workspace, diagnostics, definitions);
    };
    if (!analyzer.RegisterPass(std::move(resolvePass)))
    {
        analyzer.UnregisterPass(kUnitTypeDeclarePass);
        parsers.Unregister(kUnitTypeParser);
        templates.Unregister(kUnitTypeTemplate);
        return false;
    }
    return true;
}

}
