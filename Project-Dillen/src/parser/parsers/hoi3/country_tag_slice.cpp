#include "country_tag_slice.hpp"

#include <string>
#include <utility>

#include "country_tag_parser.hpp"
#include "file_catalog.hpp"

namespace dillen::parser::hoi3 {

namespace {

std::string ResolveDefinitionPath(
    std::string_view indexPath,
    std::string_view declaredPath
)
{
    const std::string normalizedDeclared =
        FileCatalog::NormalizeVirtualPath(declaredPath);
    if (normalizedDeclared.compare(0, 7, "common/") == 0)
    {
        return normalizedDeclared;
    }
    const std::size_t separator = indexPath.find_last_of('/');
    if (separator == std::string_view::npos)
    {
        return normalizedDeclared;
    }
    return FileCatalog::NormalizeVirtualPath(
        std::string(indexPath.substr(0, separator + 1))
            + normalizedDeclared
    );
}

bool DeclareCountryTags(
    AnalysisWorkspace& workspace,
    DiagnosticBag& diagnostics,
    content::DefinitionRegistry& definitions
)
{
    for (const ParsedFile& file : workspace.files)
    {
        if (file.result.artifact.type != kCountryTagDocumentType)
        {
            continue;
        }
        const CountryTagDocument* document =
            file.result.artifact.As<CountryTagDocument>();
        if (document == nullptr)
        {
            diagnostics.Error(
                "hoi3.country_tag.artifact_type_mismatch",
                "country tag parser returned an invalid artifact",
                {}
            );
            return false;
        }

        for (const CountryTagDeclaration& declaration
            : document->declarations)
        {
            const content::CountryTagDefinition* previous =
                definitions.Countries().Find(declaration.tag);
            if (previous != nullptr)
            {
                diagnostics.Error(
                    "hoi3.country_tag.duplicate",
                    "duplicate country tag "
                        + declaration.tag.ToString()
                        + "; first declared at "
                        + previous->origin.virtualPath
                        + ":"
                        + std::to_string(previous->origin.line),
                    declaration.tagSpan
                );
                continue;
            }

            content::CountryTagDefinition definition;
            definition.tag = declaration.tag;
            definition.id = declaration.tag.StableId();
            definition.declaredPath = declaration.declaredPath;
            definition.definitionPath = ResolveDefinitionPath(
                file.source.VirtualPath(),
                declaration.declaredPath
            );
            definition.origin.virtualPath =
                std::string(file.source.VirtualPath());
            definition.origin.sourceLayer = file.catalog.sourceLayerName;
            definition.origin.line = declaration.tagSpan.begin.line;
            definition.origin.column = declaration.tagSpan.begin.column;

            const content::CountryDeclareResult result =
                definitions.Countries().Declare(std::move(definition));
            if (result != content::CountryDeclareResult::Added)
            {
                diagnostics.Error(
                    "hoi3.country_tag.declare_failed",
                    "country tag could not be added to Definition Registry",
                    declaration.tagSpan
                );
            }
        }
    }
    return !diagnostics.HasErrors();
}

}

bool RegisterCountryTagSlice(
    TemplateRegistry& templates,
    ParserRegistry& parsers,
    Analyzer& analyzer,
    content::DefinitionRegistry& definitions
)
{
    FileTemplate fileTemplate;
    fileTemplate.id = kCountryTagTemplate;
    fileTemplate.name = "hoi3_country_tag_index";
    fileTemplate.pattern = "common/countries.txt";
    fileTemplate.parser = kCountryTagParser;
    fileTemplate.dialect = kHoi3ClausewitzDialect;
    fileTemplate.priority = 1000;
    if (!templates.Register(std::move(fileTemplate)))
    {
        return false;
    }

    ParserDescriptor parser;
    parser.id = kCountryTagParser;
    parser.name = "hoi3_country_tag_index";
    parser.inputDialect = kHoi3ClausewitzDialect;
    parser.outputType = kCountryTagDocumentType;
    parser.schemaVersion = 1;
    parser.parse = ParseCountryTagIndex;
    if (!parsers.Register(std::move(parser)))
    {
        templates.Unregister(kCountryTagTemplate);
        return false;
    }

    AnalysisPassDescriptor pass;
    pass.id = kCountryTagDeclarePass;
    pass.name = "hoi3_country_tag_declare";
    pass.phase = AnalysisPhase::Declare;
    pass.priority = -1000;
    pass.run = [&definitions](
        AnalysisWorkspace& workspace,
        DiagnosticBag& diagnostics)
    {
        return DeclareCountryTags(workspace, diagnostics, definitions);
    };
    if (!analyzer.RegisterPass(std::move(pass)))
    {
        parsers.Unregister(kCountryTagParser);
        templates.Unregister(kCountryTagTemplate);
        return false;
    }
    return true;
}

}
