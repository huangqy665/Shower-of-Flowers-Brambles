#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "analyzer.hpp"
#include "country_definition_registry.hpp"
#include "country_tag_definition.hpp"
#include "country_tag_slice.hpp"
#include "definition_registry.hpp"
#include "diagnostic.hpp"
#include "file_catalog.hpp"
#include "parser_registry.hpp"
#include "template_registry.hpp"

namespace
{

bool CopyRepositoryIndex(const std::filesystem::path& destination)
{
    namespace fs = std::filesystem;
    std::error_code error;
    fs::create_directories(destination.parent_path(), error);
    if (error)
    {
        return false;
    }
    fs::copy_file(
        fs::current_path() / "common/countries.txt",
        destination,
        fs::copy_options::overwrite_existing,
        error
    );
    return !error;
}

bool CheckRegistryRules()
{
    using namespace dillen::content;
    const auto lower = CountryTag::Parse("chi");
    const auto upper = CountryTag::Parse("CHI");
    if (!lower
        || !upper
        || *lower != *upper
        || lower->StableId() != upper->StableId()
        || CountryTag::Parse("CHINA")
        || CountryTag::Parse("C@I"))
    {
        return false;
    }

    CountryDefinitionRegistry registry;
    CountryTagDefinition definition;
    definition.id = lower->StableId();
    definition.tag = *lower;
    definition.declaredPath = "countries/Nationalist China.txt";
    definition.definitionPath =
        "common/countries/nationalist china.txt";
    if (registry.Declare(definition) != CountryDeclareResult::Added
        || registry.Declare(definition)
            != CountryDeclareResult::DuplicateTag)
    {
        return false;
    }
    registry.Freeze();
    return registry.IsFrozen()
        && registry.Find("chi") != nullptr
        && registry.Find("CHI")->id == lower->StableId()
        && registry.Declare(std::move(definition))
            == CountryDeclareResult::Frozen;
}

}

int main()
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path()
        / "project_dillen_country_tag_slice";
    std::error_code error;
    fs::remove_all(root, error);
    error.clear();
    if (!CopyRepositoryIndex(root / "common/countries.txt"))
    {
        std::cerr << "Country index fixture creation failed\n";
        return 1;
    }

    dillen::parser::TemplateRegistry templates;
    dillen::parser::ParserRegistry parsers;
    dillen::parser::Analyzer analyzer;
    dillen::content::DefinitionRegistry definitions;
    if (!dillen::parser::hoi3::RegisterCountryTagSlice(
            templates,
            parsers,
            analyzer,
            definitions))
    {
        std::cerr << "Country tag slice registration failed\n";
        return 2;
    }
    templates.Freeze();
    parsers.Freeze();
    analyzer.Freeze();

    dillen::parser::DiagnosticBag diagnostics;
    dillen::parser::FileCatalog catalog;
    if (!catalog.AddLayer({1, "repository", root, 0, {}})
        || !catalog.Build(templates, diagnostics)
        || catalog.ActiveClassifiedFileCount() != 1)
    {
        std::cerr << "Country tag catalog construction failed\n";
        return 3;
    }

    dillen::parser::AnalysisWorkspace workspace;
    if (!analyzer.Analyze(
            catalog,
            parsers,
            workspace,
            diagnostics))
    {
        for (const auto& diagnostic : diagnostics.All())
        {
            std::cerr
                << dillen::parser::FormatDiagnostic(diagnostic)
                << '\n';
        }
        std::cerr << "Country tag analysis failed\n";
        return 4;
    }
    definitions.Freeze();

    const auto* china = definitions.Countries().Find("CHI");
    const auto* japan = definitions.Countries().Find("jap");
    const bool valid = definitions.IsFrozen()
        && definitions.Countries().IsFrozen()
        && definitions.Countries().Size() == 142
        && china != nullptr
        && japan != nullptr
        && china->id == china->tag.StableId()
        && china->declaredPath == "countries/Nationalist China.txt"
        && china->definitionPath
            == "common/countries/nationalist china.txt"
        && china->origin.virtualPath == "common/countries.txt"
        && china->origin.sourceLayer == "repository"
        && japan->definitionPath == "common/countries/japan.txt"
        && CheckRegistryRules();

    fs::remove_all(root, error);
    if (!valid)
    {
        std::cerr << "Country tag Definition Registry validation failed\n";
        return 5;
    }

    std::cout
        << "Country tag slice: passed ("
        << definitions.Countries().Size()
        << " definitions)\n";
    return 0;
}
