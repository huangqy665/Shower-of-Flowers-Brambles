#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

#include "analyzer.hpp"
#include "country_tag_definition.hpp"
#include "country_tag_slice.hpp"
#include "definition_registry.hpp"
#include "diagnostic.hpp"
#include "file_catalog.hpp"
#include "parser_registry.hpp"
#include "technology_slice.hpp"
#include "template_registry.hpp"
#include "unit_model_definition.hpp"
#include "unit_model_definition_registry.hpp"
#include "unit_model_slice.hpp"
#include "unit_type_slice.hpp"

namespace
{

bool CopyFile(
    const std::filesystem::path& source,
    const std::filesystem::path& destination
)
{
    std::error_code error;
    std::filesystem::create_directories(destination.parent_path(), error);
    if (error)
    {
        return false;
    }
    std::filesystem::copy_file(
        source,
        destination,
        std::filesystem::copy_options::overwrite_existing,
        error
    );
    return !error;
}

bool CopyDirectory(
    const std::filesystem::path& source,
    const std::filesystem::path& destination
)
{
    namespace fs = std::filesystem;
    std::error_code error;
    for (fs::recursive_directory_iterator iterator(source, error), end;
        iterator != end && !error;
        iterator.increment(error))
    {
        const fs::path relative = fs::relative(iterator->path(), source, error);
        if (error)
        {
            return false;
        }
        const fs::path target = destination / relative;
        if (iterator->is_directory())
        {
            fs::create_directories(target, error);
        }
        else if (iterator->is_regular_file())
        {
            fs::create_directories(target.parent_path(), error);
            if (!error)
            {
                fs::copy_file(
                    iterator->path(),
                    target,
                    fs::copy_options::overwrite_existing,
                    error
                );
            }
        }
    }
    return !error;
}

bool CopyRepositoryFixture(const std::filesystem::path& root)
{
    const std::filesystem::path repository = std::filesystem::current_path();
    return CopyFile(
            repository / "common/countries.txt",
            root / "common/countries.txt")
        && CopyDirectory(repository / "units", root / "units")
        && CopyDirectory(
            repository / "technologies",
            root / "technologies");
}

std::size_t CountDiagnostic(
    const dillen::parser::DiagnosticBag& diagnostics,
    const std::string& code
)
{
    return static_cast<std::size_t>(std::count_if(
        diagnostics.All().begin(),
        diagnostics.All().end(),
        [&code](const dillen::parser::Diagnostic& diagnostic)
        {
            return diagnostic.code == code;
        }
    ));
}

const dillen::content::UnitModelTechnologyLevel* FindLevel(
    const dillen::content::UnitModelDefinition& definition,
    const std::string& name
)
{
    const auto iterator = std::find_if(
        definition.technologyLevels.begin(),
        definition.technologyLevels.end(),
        [&name](const dillen::content::UnitModelTechnologyLevel& level)
        {
            return level.name == name;
        }
    );
    return iterator == definition.technologyLevels.end()
        ? nullptr
        : &*iterator;
}

void PrintDiagnostics(
    const dillen::parser::AnalysisWorkspace& workspace,
    const dillen::parser::DiagnosticBag& diagnostics
)
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
            << dillen::parser::FormatDiagnostic(diagnostic, virtualPath)
            << '\n';
    }
}

}

int main()
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path()
        / ("project_dillen_unit_model_"
            + std::to_string(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()
            ));
    if (!CopyRepositoryFixture(root))
    {
        std::cerr << "Unit model fixture creation failed\n";
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
            definitions)
        || !dillen::parser::hoi3::RegisterUnitTypeSlice(
            templates,
            parsers,
            analyzer,
            definitions)
        || !dillen::parser::hoi3::RegisterTechnologySlice(
            templates,
            parsers,
            analyzer,
            definitions)
        || !dillen::parser::hoi3::RegisterUnitModelSlice(
            templates,
            parsers,
            analyzer,
            definitions))
    {
        std::cerr << "Unit model slice registration failed\n";
        return 2;
    }
    templates.Freeze();
    parsers.Freeze();
    analyzer.Freeze();

    dillen::parser::DiagnosticBag diagnostics;
    dillen::parser::FileCatalog catalog;
    if (!catalog.AddLayer({1, "repository", root, 0, {}})
        || !catalog.Build(templates, diagnostics)
        || catalog.ActiveClassifiedFileCount() != 59)
    {
        std::cerr
            << "Unit model catalog failed: active="
            << catalog.ActiveClassifiedFileCount()
            << '\n';
        return 3;
    }

    dillen::parser::AnalysisWorkspace workspace;
    if (!analyzer.Analyze(catalog, parsers, workspace, diagnostics))
    {
        PrintDiagnostics(workspace, diagnostics);
        std::cerr << "Unit model analysis failed\n";
        return 4;
    }
    definitions.Freeze();

    const auto japanTag = dillen::content::CountryTag::Parse("JAP");
    const auto usaTag = dillen::content::CountryTag::Parse("USA");
    const auto* japanInterceptor = japanTag
        ? definitions.UnitModels().Find(
            japanTag->StableId(),
            "interceptor",
            2)
        : nullptr;
    const auto* usaInterceptor = usaTag
        ? definitions.UnitModels().Find(
            usaTag->StableId(),
            "INTERCEPTOR",
            2)
        : nullptr;
    const auto* usaCarrier = usaTag
        ? definitions.UnitModels().Find(
            usaTag->StableId(),
            "carrier",
            4)
        : nullptr;
    const auto* japanSeaplaneCarrier = japanTag
        ? definitions.UnitModels().Find(
            japanTag->StableId(),
            "seaplane_carrier",
            1)
        : nullptr;
    const auto* japanAeroengine = japanInterceptor == nullptr
        ? nullptr
        : FindLevel(*japanInterceptor, "aeroengine");
    const auto* usaAeroengine = usaInterceptor == nullptr
        ? nullptr
        : FindLevel(*usaInterceptor, "aeroengine");
    const auto* carrierHanger = usaCarrier == nullptr
        ? nullptr
        : FindLevel(*usaCarrier, "carrier_hanger");
    const bool valid = definitions.Countries().Size() == 142
        && definitions.UnitTypes().Size() == 46
        && definitions.Technologies().Size() == 249
        && definitions.UnitModels().Size() == 154
        && definitions.UnitModels().ResolvedCount() == 154
        && japanInterceptor != nullptr
        && usaInterceptor != nullptr
        && japanInterceptor->id != usaInterceptor->id
        && !japanInterceptor->unitType.has_value()
        && !usaInterceptor->unitType.has_value()
        && japanAeroengine != nullptr
        && japanAeroengine->level == 2
        && japanAeroengine->technology.has_value()
        && usaAeroengine != nullptr
        && usaAeroengine->level == 1
        && usaAeroengine->technology.has_value()
        && usaCarrier != nullptr
        && usaCarrier->unitType.has_value()
        && carrierHanger != nullptr
        && carrierHanger->level == 4
        && carrierHanger->technology.has_value()
        && japanSeaplaneCarrier != nullptr
        && japanSeaplaneCarrier->unitType.has_value()
        && japanSeaplaneCarrier->origin.virtualPath
            == "units/models/jap - ships.txt"
        && CountDiagnostic(
            diagnostics,
            "hoi3.unit_model.duplicate_identical_ignored") == 1
        && definitions.UnitModels().Declare({})
            == dillen::content::UnitModelDeclareResult::Frozen
        && definitions.UnitModels().ResolveReferences({}, {}, {})
            == dillen::content::UnitModelResolveResult::Frozen;

    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    if (!valid)
    {
        PrintDiagnostics(workspace, diagnostics);
        std::cerr
            << "countries=" << definitions.Countries().Size()
            << " units=" << definitions.UnitTypes().Size()
            << " technologies=" << definitions.Technologies().Size()
            << " models=" << definitions.UnitModels().Size()
            << " resolved=" << definitions.UnitModels().ResolvedCount()
            << " duplicate=" << CountDiagnostic(
                diagnostics,
                "hoi3.unit_model.duplicate_identical_ignored")
            << '\n';
        std::cerr << "Unit model Registry validation failed\n";
        return 5;
    }

    std::cout
        << "Unit model slice: passed ("
        << definitions.UnitModels().Size()
        << " unique UnitModelDefinitions from 4 files)\n";
    return 0;
}
