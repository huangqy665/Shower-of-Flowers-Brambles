#include "region_definition_slice.hpp"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "country_tag_slice.hpp"
#include "region_definition_parser.hpp"

namespace dillen::parser::hoi3 {

namespace {

bool DeclareRegions(
    AnalysisWorkspace& workspace,
    DiagnosticBag& diagnostics,
    content::DefinitionRegistry& definitions
)
{
    for (const ParsedFile& file : workspace.files)
    {
        if (file.result.artifact.type != kRegionDefinitionDocumentType)
        {
            continue;
        }
        const RegionDefinitionDocument* document =
            file.result.artifact.As<RegionDefinitionDocument>();
        if (document == nullptr)
        {
            diagnostics.Error(
                "hoi3.region.artifact_type_mismatch",
                "Region parser returned an invalid artifact"
            );
            return false;
        }

        for (const RegionDefinitionDeclaration& declaration
            : document->declarations)
        {
            content::RegionDefinition definition;
            definition.id = content::StableRegionDefinitionId(
                declaration.name
            );
            definition.name = declaration.name;
            definition.origin.virtualPath =
                std::string(file.source.VirtualPath());
            definition.origin.sourceLayer =
                file.catalog.sourceLayerName;
            definition.origin.line = declaration.nameSpan.begin.line;
            definition.origin.column = declaration.nameSpan.begin.column;
            const content::RegionDeclareResult result =
                definitions.Regions().Declare(std::move(definition));
            if (result != content::RegionDeclareResult::Added)
            {
                diagnostics.Error(
                    "hoi3.region.declare_failed",
                    "Region definition could not be added to the Registry",
                    declaration.nameSpan
                );
            }
        }
    }
    return !diagnostics.HasErrors();
}

bool ResolveRegions(
    AnalysisWorkspace& workspace,
    DiagnosticBag& diagnostics,
    content::DefinitionRegistry& definitions
)
{
    for (const ParsedFile& file : workspace.files)
    {
        if (file.result.artifact.type != kRegionDefinitionDocumentType)
        {
            continue;
        }
        const RegionDefinitionDocument* document =
            file.result.artifact.As<RegionDefinitionDocument>();
        if (document == nullptr)
        {
            diagnostics.Error(
                "hoi3.region.artifact_type_mismatch",
                "Region parser returned an invalid artifact"
            );
            return false;
        }

        for (const RegionDefinitionDeclaration& declaration
            : document->declarations)
        {
            std::unordered_set<std::uint32_t> seenProvinces;
            std::vector<content::ProvinceDefinitionId> provinces;
            provinces.reserve(declaration.provinces.size());
            bool missingReference = false;
            for (const RegionProvinceReference& reference
                : declaration.provinces)
            {
                if (!seenProvinces.emplace(reference.value).second)
                {
                    diagnostics.Warning(
                        "hoi3.region.province_duplicate",
                        "duplicate Province ID in Region '"
                            + declaration.name + "' was ignored",
                        reference.span
                    );
                    continue;
                }
                if (definitions.Provinces().Find(reference.value) == nullptr)
                {
                    diagnostics.Error(
                        "hoi3.region.province_missing",
                        "Region '" + declaration.name
                            + "' references unknown Province ID "
                            + std::to_string(reference.value),
                        reference.span
                    );
                    missingReference = true;
                    continue;
                }
                provinces.push_back({reference.value});
            }

            std::unordered_set<std::string> seenFlags;
            std::vector<std::string> flags;
            flags.reserve(declaration.flags.size());
            for (const RegionFlagDeclaration& flag : declaration.flags)
            {
                if (!seenFlags.emplace(flag.name).second)
                {
                    diagnostics.Warning(
                        "hoi3.region.flag_duplicate",
                        "duplicate Region flag '" + flag.name
                            + "' was ignored",
                        flag.span
                    );
                    continue;
                }
                flags.push_back(flag.name);
            }
            if (missingReference)
            {
                continue;
            }

            const content::RegionResolveResult result =
                definitions.Regions().Resolve(
                    content::StableRegionDefinitionId(declaration.name),
                    std::move(provinces),
                    std::move(flags)
                );
            if (result != content::RegionResolveResult::Resolved)
            {
                diagnostics.Error(
                    "hoi3.region.resolve_failed",
                    "Region definition could not resolve Province references",
                    declaration.nameSpan
                );
            }
        }
    }
    return !diagnostics.HasErrors();
}

}

bool RegisterRegionDefinitionSlice(
    TemplateRegistry& templates,
    ParserRegistry& parsers,
    Analyzer& analyzer,
    content::DefinitionRegistry& definitions
)
{
    FileTemplate fileTemplate;
    fileTemplate.id = kRegionDefinitionTemplate;
    fileTemplate.name = "hoi3_region_definition";
    fileTemplate.pattern = "map/region.txt";
    fileTemplate.parser = kRegionDefinitionParser;
    fileTemplate.dialect = kHoi3ClausewitzDialect;
    fileTemplate.priority = 1000;
    if (!templates.Register(std::move(fileTemplate)))
    {
        return false;
    }

    ParserDescriptor parser;
    parser.id = kRegionDefinitionParser;
    parser.name = "hoi3_region_definition";
    parser.inputDialect = kHoi3ClausewitzDialect;
    parser.outputType = kRegionDefinitionDocumentType;
    parser.schemaVersion = 1;
    parser.parse = ParseRegionDefinitions;
    if (!parsers.Register(std::move(parser)))
    {
        templates.Unregister(kRegionDefinitionTemplate);
        return false;
    }

    AnalysisPassDescriptor declarePass;
    declarePass.id = kRegionDefinitionDeclarePass;
    declarePass.name = "hoi3_region_definition_declare";
    declarePass.phase = AnalysisPhase::Declare;
    declarePass.priority = -1900;
    declarePass.run = [&definitions](
        AnalysisWorkspace& workspace,
        DiagnosticBag& diagnostics)
    {
        return DeclareRegions(workspace, diagnostics, definitions);
    };
    if (!analyzer.RegisterPass(std::move(declarePass)))
    {
        parsers.Unregister(kRegionDefinitionParser);
        templates.Unregister(kRegionDefinitionTemplate);
        return false;
    }

    AnalysisPassDescriptor resolvePass;
    resolvePass.id = kRegionDefinitionResolvePass;
    resolvePass.name = "hoi3_region_definition_resolve";
    resolvePass.phase = AnalysisPhase::Resolve;
    resolvePass.priority = -2000;
    resolvePass.run = [&definitions](
        AnalysisWorkspace& workspace,
        DiagnosticBag& diagnostics)
    {
        return ResolveRegions(workspace, diagnostics, definitions);
    };
    if (!analyzer.RegisterPass(std::move(resolvePass)))
    {
        analyzer.UnregisterPass(kRegionDefinitionDeclarePass);
        parsers.Unregister(kRegionDefinitionParser);
        templates.Unregister(kRegionDefinitionTemplate);
        return false;
    }
    return true;
}

}
