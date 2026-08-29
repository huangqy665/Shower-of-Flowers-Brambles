#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

#include "resolver.hpp"
#include "country_definition.hpp"
#include "country_definition_parser.hpp"
#include "country_definition_slice.hpp"
#include "country_tag_slice.hpp"
#include "definition_registry.hpp"
#include "diagnostic.hpp"
#include "file_catalog.hpp"
#include "parser_registry.hpp"
#include "template_registry.hpp"

namespace
{

bool CopyRepositoryCountryData(const std::filesystem::path& root)
{
    namespace fs = std::filesystem;
    std::error_code error;
    fs::create_directories(root / "common/countries", error);
    if (error)
    {
        return false;
    }
    fs::copy_file(
        fs::current_path() / "common/countries.txt",
        root / "common/countries.txt",
        fs::copy_options::overwrite_existing,
        error
    );
    if (error)
    {
        return false;
    }
    for (const fs::directory_entry& entry
        : fs::directory_iterator(fs::current_path() / "common/countries"))
    {
        if (!entry.is_regular_file()
            || entry.path().extension() != ".txt")
        {
            continue;
        }
        fs::copy_file(
            entry.path(),
            root / "common/countries" / entry.path().filename(),
            fs::copy_options::overwrite_existing,
            error
        );
        if (error)
        {
            return false;
        }
    }
    return true;
}

bool HasDiagnostic(
    const dillen::parser::DiagnosticBag& diagnostics,
    const std::string& code
)
{
    for (const auto& diagnostic : diagnostics.All())
    {
        if (diagnostic.code == code)
        {
            return true;
        }
    }
    return false;
}

bool ValidateRecoveryParser()
{
    const std::string text =
        "color = { 1 2 3 }\n"
        "graphical_culture = test\n"
        "unit_names = {\n"
        "  light_armor_brigade = {\n"
        "    \"First Light Division\"\n"
        "  armor_brigade = { \"First Armor Division\" }\n"
        "}\n"
        "ministers = {\n"
        "  1 = {\n"
        "    name = \"Minister\"\n"
        "    ideology = test\n"
        "    loyalty = 1.0\n"
        "    picture = M1\n"
        "    head_of_state = test_trait\n"
        "    start_date = 1936.1.1\n"
        "  }\n";
    dillen::parser::SourceBuffer source(
        1,
        "probe/country_recovery.txt",
        "probe/country_recovery.txt",
        text
    );
    dillen::parser::DiagnosticBag diagnostics;
    dillen::parser::ParserCursor cursor(source, diagnostics);
    dillen::parser::ParseArtifact artifact;
    if (!dillen::parser::hoi3::ParseCountryDefinition(
            cursor,
            artifact))
    {
        return false;
    }
    const auto* document = artifact.As<
        dillen::parser::hoi3::CountryDefinitionDocument>();
    return document != nullptr
        && !diagnostics.HasErrors()
        && document->definition.unitNamePools.size() == 2
        && document->definition.ministers.size() == 1
        && HasDiagnostic(
            diagnostics,
            "hoi3.country.unit_name_pool_missing_right_brace"
        )
        && HasDiagnostic(
            diagnostics,
            "hoi3.country.ministers_missing_right_brace"
        );
}

}

int main()
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path()
        / ("project_dillen_country_definition_"
            + std::to_string(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()
            ));
    if (!CopyRepositoryCountryData(root))
    {
        std::cerr << "Country definition fixture creation failed\n";
        return 1;
    }

    dillen::parser::TemplateRegistry templates;
    dillen::parser::ParserRegistry parsers;
    dillen::parser::Resolver resolver;
    dillen::compatibility::hoi3::content::DefinitionRegistry definitions;
    if (!dillen::parser::hoi3::RegisterCountryTagSlice(
            templates,
            parsers,
            resolver,
            definitions)
        || !dillen::parser::hoi3::RegisterCountryDefinitionSlice(
            templates,
            parsers,
            resolver,
            definitions))
    {
        std::cerr << "Country definition slice registration failed\n";
        return 2;
    }
    templates.Freeze();
    parsers.Freeze();
    resolver.Freeze();

    dillen::parser::DiagnosticBag diagnostics;
    dillen::parser::FileCatalog catalog;
    if (!catalog.AddLayer({1, "repository", root, 0, {}})
        || !catalog.Build(templates, diagnostics)
        || catalog.ActiveClassifiedFileCount() != 51)
    {
        std::cerr << "Country definition catalog failed\n";
        return 3;
    }

    dillen::parser::ParseWorkspace workspace;
    if (!(catalog.Parse(parsers, workspace, diagnostics) && resolver.Resolve(workspace, diagnostics)))
    {
        for (const auto& diagnostic : diagnostics.All())
        {
            std::string_view virtualPath;
            for (const auto& file : workspace.files)
            {
                if (file.source.Id() == diagnostic.span.begin.source)
                {
                    virtualPath = file.source.VirtualPath();
                    break;
                }
            }
            std::cerr
                << dillen::parser::FormatDiagnostic(
                    diagnostic,
                    virtualPath
                )
                << '\n';
        }
        std::cerr << "Country definition analysis failed\n";
        return 4;
    }
    definitions.Freeze();

    const auto* china = definitions.Countries().Find("CHI");
    const auto* chinaAlias = definitions.Countries().Find("CSH");
    const auto* japan = definitions.Countries().Find("JAP");
    const auto* finland = definitions.Countries().Find("FIN");
    const auto* mengkukuo = definitions.Countries().Find("MEN");
    const auto* mengkukuoAlias = definitions.Countries().Find("MEB");
    const auto* unresolved = definitions.Countries().Find("USA");
    const bool valid = ValidateRecoveryParser()
        && definitions.Countries().Size() == 142
        && definitions.Countries().ResolvedCount() == 58
        && china != nullptr
        && chinaAlias != nullptr
        && china->definition != nullptr
        && china->definition == chinaAlias->definition
        && china->definition->color.has_value()
        && china->definition->color->red == 29
        && china->definition->color->green == 159
        && china->definition->color->blue == 201
        && china->definition->graphicalCulture == "Japanese"
        && !china->definition->defaultTemplates.empty()
        && !china->definition->unitNamePools.empty()
        && japan != nullptr
        && japan->definition != nullptr
        && japan->definition->major
        && !japan->definition->ministers.empty()
        && finland != nullptr
        && finland->definition != nullptr
        && finland->definition->lastElection.has_value()
        && finland->definition->lastElection->year == 1933
        && finland->definition->electionDurationMonths == 36
        && mengkukuo != nullptr
        && mengkukuoAlias != nullptr
        && mengkukuo->definition == mengkukuoAlias->definition
        && unresolved != nullptr
        && unresolved->definition == nullptr
        && HasDiagnostic(
            diagnostics,
            "hoi3.country.definition_unreferenced"
        );

    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    if (!valid)
    {
        std::cerr << "Country definition Registry validation failed\n";
        return 5;
    }

    std::cout
        << "Country definition slice: passed ("
        << definitions.Countries().ResolvedCount()
        << "/"
        << definitions.Countries().Size()
        << " Tags resolved)\n";
    return 0;
}
