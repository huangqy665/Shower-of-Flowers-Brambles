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
    ParseWorkspace& workspace,
    DiagnosticBag& diagnostics,
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions
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
            const dillen::compatibility::hoi3::content::CountryTagDefinition* previous =
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

            dillen::compatibility::hoi3::content::CountryTagDefinition definition;
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

            const dillen::compatibility::hoi3::content::CountryDeclareResult result =
                definitions.Countries().Declare(std::move(definition));
            if (result != dillen::compatibility::hoi3::content::CountryDeclareResult::Added)
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
    Resolver& resolver,
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions
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

    ResolutionPassDescriptor pass;
    pass.id = kCountryTagDeclarePass;
    pass.name = "hoi3_country_tag_declare";
    pass.phase = ResolutionPhase::Declare;
    pass.priority = -1000;
    pass.run = [&definitions](
        ParseWorkspace& workspace,
        DiagnosticBag& diagnostics)
    {
        return DeclareCountryTags(workspace, diagnostics, definitions);
    };
    if (!resolver.RegisterPass(std::move(pass)))
    {
        parsers.Unregister(kCountryTagParser);
        templates.Unregister(kCountryTagTemplate);
        return false;
    }
    return true;
}

}
