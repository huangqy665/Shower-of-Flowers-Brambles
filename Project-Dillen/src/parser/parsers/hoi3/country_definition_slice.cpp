#include "country_definition_slice.hpp"

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "country_definition_parser.hpp"
#include "country_tag_slice.hpp"

namespace dillen::parser::hoi3 {

namespace {

bool ResolveCountryDefinitions(
    ParseWorkspace& workspace,
    DiagnosticBag& diagnostics,
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions
)
{
    for (ParsedFile& file : workspace.files)
    {
        if (file.result.artifact.type != kCountryDefinitionDocumentType)
        {
            continue;
        }
        CountryDefinitionDocument* document =
            file.result.artifact.As<CountryDefinitionDocument>();
        if (document == nullptr)
        {
            diagnostics.Error(
                "hoi3.country.artifact_type_mismatch",
                "country definition parser returned an invalid artifact"
            );
            return false;
        }

        document->definition.virtualPath =
            std::string(file.source.VirtualPath());
        document->definition.origin.virtualPath =
            document->definition.virtualPath;
        document->definition.origin.sourceLayer =
            file.catalog.sourceLayerName;
        document->definition.origin.line = 1;
        document->definition.origin.column = 1;

        std::vector<dillen::compatibility::hoi3::content::CountryDefinitionId> consumers;
        for (const dillen::compatibility::hoi3::content::CountryTagDefinition& country
            : definitions.Countries().All())
        {
            if (country.definitionPath == document->definition.virtualPath)
            {
                consumers.push_back(country.id);
            }
        }
        if (consumers.empty())
        {
            diagnostics.Warning(
                "hoi3.country.definition_unreferenced",
                "country definition file is not referenced by common/countries.txt: "
                    + document->definition.virtualPath
            );
            continue;
        }

        std::shared_ptr<const dillen::compatibility::hoi3::content::CountryDefinition> resolved =
            std::make_shared<dillen::compatibility::hoi3::content::CountryDefinition>(
                std::move(document->definition)
            );
        for (dillen::compatibility::hoi3::content::CountryDefinitionId id : consumers)
        {
            const dillen::compatibility::hoi3::content::CountryResolveResult result =
                definitions.Countries().Resolve(id, resolved);
            if (result != dillen::compatibility::hoi3::content::CountryResolveResult::Resolved)
            {
                diagnostics.Error(
                    "hoi3.country.resolve_failed",
                    "country definition could not be attached to its Tag"
                );
            }
        }
    }
    return !diagnostics.HasErrors();
}

}

bool RegisterCountryDefinitionSlice(
    TemplateRegistry& templates,
    ParserRegistry& parsers,
    Resolver& resolver,
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions
)
{
    FileTemplate fileTemplate;
    fileTemplate.id = kCountryDefinitionTemplate;
    fileTemplate.name = "hoi3_country_definition";
    fileTemplate.pattern = "common/countries/*.txt";
    fileTemplate.parser = kCountryDefinitionParser;
    fileTemplate.dialect = kHoi3ClausewitzDialect;
    fileTemplate.priority = 900;
    if (!templates.Register(std::move(fileTemplate)))
    {
        return false;
    }

    ParserDescriptor parser;
    parser.id = kCountryDefinitionParser;
    parser.name = "hoi3_country_definition";
    parser.inputDialect = kHoi3ClausewitzDialect;
    parser.outputType = kCountryDefinitionDocumentType;
    parser.schemaVersion = 1;
    parser.parse = ParseCountryDefinition;
    if (!parsers.Register(std::move(parser)))
    {
        templates.Unregister(kCountryDefinitionTemplate);
        return false;
    }

    ResolutionPassDescriptor pass;
    pass.id = kCountryDefinitionResolvePass;
    pass.name = "hoi3_country_definition_resolve";
    pass.phase = ResolutionPhase::Resolve;
    pass.priority = -900;
    pass.run = [&definitions](
        ParseWorkspace& workspace,
        DiagnosticBag& diagnostics)
    {
        return ResolveCountryDefinitions(
            workspace,
            diagnostics,
            definitions
        );
    };
    if (!resolver.RegisterPass(std::move(pass)))
    {
        parsers.Unregister(kCountryDefinitionParser);
        templates.Unregister(kCountryDefinitionTemplate);
        return false;
    }
    return true;
}

}
