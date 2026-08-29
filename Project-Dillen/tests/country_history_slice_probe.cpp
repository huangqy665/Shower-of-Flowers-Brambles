#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

#include "resolver.hpp"
#include "country_history.hpp"
#include "country_history_registry.hpp"
#include "country_history_slice.hpp"
#include "country_tag_definition.hpp"
#include "country_tag_slice.hpp"
#include "definition_registry.hpp"
#include "diagnostic.hpp"
#include "file_catalog.hpp"
#include "parser_registry.hpp"
#include "province_definition_slice.hpp"
#include "template_registry.hpp"

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
            repository / "map/definition.csv",
            root / "map/definition.csv")
        && CopyFile(
            repository / "common/countries.txt",
            root / "common/countries.txt")
        && CopyDirectory(
            repository / "history/countries",
            root / "history/countries");
}

bool WriteText(
    const std::filesystem::path& path,
    const std::string& text
)
{
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
    {
        return false;
    }
    std::ofstream stream(path, std::ios::binary);
    stream << text;
    return stream.good();
}

bool HasDiagnostic(
    const dillen::parser::DiagnosticBag& diagnostics,
    const std::string& code
)
{
    return std::any_of(
        diagnostics.All().begin(),
        diagnostics.All().end(),
        [&code](const dillen::parser::Diagnostic& diagnostic)
        {
            return diagnostic.code == code;
        }
    );
}

const dillen::compatibility::hoi3::content::CountryHistoryOperation* FindOperation(
    const std::vector<dillen::compatibility::hoi3::content::CountryHistoryOperation>& operations,
    dillen::compatibility::hoi3::content::CountryHistoryField field,
    const std::string& key = {}
)
{
    const auto iterator = std::find_if(
        operations.begin(),
        operations.end(),
        [field, &key](
            const dillen::compatibility::hoi3::content::CountryHistoryOperation& operation)
        {
            return operation.field == field
                && (key.empty() || operation.key == key);
        }
    );
    return iterator == operations.end() ? nullptr : &*iterator;
}

const dillen::compatibility::hoi3::content::CountryHistoryPatch* FindPatch(
    const dillen::compatibility::hoi3::content::CountryHistoryTimeline& timeline,
    dillen::compatibility::hoi3::content::DefinitionDate date
)
{
    const auto iterator = std::find_if(
        timeline.patches.begin(),
        timeline.patches.end(),
        [date](const dillen::compatibility::hoi3::content::CountryHistoryPatch& patch)
        {
            return patch.date == date;
        }
    );
    return iterator == timeline.patches.end() ? nullptr : &*iterator;
}

bool NamedMapContains(
    const dillen::compatibility::hoi3::content::CountryHistoryOperation* operation,
    const std::string& name,
    double expected
)
{
    if (operation == nullptr)
    {
        return false;
    }
    const auto* map = std::get_if<
        dillen::compatibility::hoi3::content::CountryHistoryNamedNumberMap>(&operation->value);
    return map != nullptr && std::any_of(
        map->values.begin(),
        map->values.end(),
        [&name, expected](
            const dillen::compatibility::hoi3::content::CountryHistoryNamedNumber& value)
        {
            return value.name == name
                && std::abs(value.value - expected) < 0.0001;
        }
    );
}

bool ValidateMissingCapitalDiagnostic()
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path()
        / ("project_dillen_missing_country_capital_"
            + std::to_string(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()
            ));
    if (!WriteText(
            root / "map/definition.csv",
            "province;red;green;blue;x;x\n"
            "1;1;2;3;one;x\n")
        || !WriteText(
            root / "history/countries/AAA - Test.txt",
            "capital = 2\n"))
    {
        return false;
    }

    dillen::parser::TemplateRegistry templates;
    dillen::parser::ParserRegistry parsers;
    dillen::parser::Resolver resolver;
    dillen::compatibility::hoi3::content::DefinitionRegistry definitions;
    if (!dillen::parser::hoi3::RegisterProvinceDefinitionSlice(
            templates,
            parsers,
            resolver,
            definitions)
        || !dillen::parser::hoi3::RegisterCountryHistorySlice(
            templates,
            parsers,
            resolver,
            definitions))
    {
        return false;
    }
    templates.Freeze();
    parsers.Freeze();
    resolver.Freeze();

    dillen::parser::DiagnosticBag diagnostics;
    dillen::parser::FileCatalog catalog;
    dillen::parser::ParseWorkspace workspace;
    const bool valid = catalog.AddLayer({1, "fixture", root, 0, {}})
        && catalog.Build(templates, diagnostics)
        && !(catalog.Parse(parsers, workspace, diagnostics) && resolver.Resolve(workspace, diagnostics))
        && HasDiagnostic(
            diagnostics,
            "hoi3.country_history.capital_province_missing")
        && definitions.Provinces().Size() == 1
        && definitions.CountryHistories().Size() == 0;
    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    return valid;
}

void PrintDiagnostics(
    const dillen::parser::ParseWorkspace& workspace,
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
    using dillen::compatibility::hoi3::content::CountryHistoryField;
    const fs::path root = fs::temp_directory_path()
        / ("project_dillen_country_history_"
            + std::to_string(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()
            ));
    if (!CopyRepositoryFixture(root))
    {
        std::cerr << "Country history fixture creation failed\n";
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
        || !dillen::parser::hoi3::RegisterProvinceDefinitionSlice(
            templates,
            parsers,
            resolver,
            definitions)
        || !dillen::parser::hoi3::RegisterCountryHistorySlice(
            templates,
            parsers,
            resolver,
            definitions))
    {
        std::cerr << "Country history slice registration failed\n";
        return 2;
    }
    templates.Freeze();
    parsers.Freeze();
    resolver.Freeze();

    dillen::parser::DiagnosticBag diagnostics;
    dillen::parser::FileCatalog catalog;
    if (!catalog.AddLayer({1, "repository", root, 0, {}})
        || !catalog.Build(templates, diagnostics)
        || catalog.ActiveClassifiedFileCount() != 104)
    {
        std::cerr
            << "Country history catalog failed: active="
            << catalog.ActiveClassifiedFileCount()
            << '\n';
        return 3;
    }

    dillen::parser::ParseWorkspace workspace;
    if (!(catalog.Parse(parsers, workspace, diagnostics) && resolver.Resolve(workspace, diagnostics)))
    {
        PrintDiagnostics(workspace, diagnostics);
        std::cerr << "Country history analysis failed\n";
        return 4;
    }
    definitions.Freeze();

    const auto* china = definitions.CountryHistories().Find("CHI");
    const auto* yunnan = definitions.CountryHistories().Find("CYN");
    const auto* chinaCapital = china == nullptr
        ? nullptr
        : FindOperation(
            china->initialOperations,
            CountryHistoryField::Capital);
    const auto* chinaGovernment = china == nullptr
        ? nullptr
        : FindOperation(
            china->initialOperations,
            CountryHistoryField::Government);
    const auto* chinaAlignment = china == nullptr
        ? nullptr
        : FindOperation(
            china->initialOperations,
            CountryHistoryField::Alignment);
    const auto* chinaPopularity = china == nullptr
        ? nullptr
        : FindOperation(
            china->initialOperations,
            CountryHistoryField::Popularity);
    const auto* chinaTheory = china == nullptr
        ? nullptr
        : FindOperation(
            china->initialOperations,
            CountryHistoryField::NamedAssignment,
            "infantry_theory");
    const auto* chinaPatch = china == nullptr
        ? nullptr
        : FindPatch(*china, {1932, 6, 1});
    const auto* yunnanPatch = yunnan == nullptr
        ? nullptr
        : FindPatch(*yunnan, {1938, 1, 1});
    const auto* alliance = yunnanPatch == nullptr
        ? nullptr
        : FindOperation(
            yunnanPatch->operations,
            CountryHistoryField::CreateAlliance);
    std::size_t patchCount = 0;
    for (const auto& timeline : definitions.CountryHistories().All())
    {
        patchCount += timeline.patches.size();
    }

    const auto* capital = chinaCapital == nullptr
        ? nullptr
        : std::get_if<dillen::compatibility::hoi3::content::ProvinceDefinitionId>(
            &chinaCapital->value);
    const auto* government = chinaGovernment == nullptr
        ? nullptr
        : std::get_if<std::string>(&chinaGovernment->value);
    const auto* alignment = chinaAlignment == nullptr
        ? nullptr
        : std::get_if<dillen::compatibility::hoi3::content::CountryAlignment>(
            &chinaAlignment->value);
    const auto* theory = chinaTheory == nullptr
        ? nullptr
        : std::get_if<std::int64_t>(&chinaTheory->value);
    const auto* allianceCountry = alliance == nullptr
        ? nullptr
        : std::get_if<dillen::compatibility::hoi3::content::CountryDefinitionId>(
            &alliance->value);
    const auto expectedAlliance =
        dillen::compatibility::hoi3::content::CountryTag::Parse("CGX");
    const bool valid = ValidateMissingCapitalDiagnostic()
        && definitions.Provinces().Size() == 14187
        && definitions.CountryHistories().Size() == 102
        && definitions.CountryHistories().SourceCount() == 102
        && patchCount == 488
        && china != nullptr
        && capital != nullptr
        && capital->value == 5494
        && government != nullptr
        && *government == "chinese_warlord"
        && alignment != nullptr
        && std::abs(alignment->x) < 0.0001
        && std::abs(alignment->y) < 0.0001
        && NamedMapContains(
            chinaPopularity,
            "paternal_autocrat",
            62.0)
        && theory != nullptr
        && *theory == 5
        && chinaPatch != nullptr
        && FindOperation(
            chinaPatch->operations,
            CountryHistoryField::SetCountryFlag) != nullptr
        && allianceCountry != nullptr
        && expectedAlliance
        && *allianceCountry == expectedAlliance->StableId()
        && HasDiagnostic(
            diagnostics,
            "hoi3.country_history.filename_noncanonical")
        && HasDiagnostic(
            diagnostics,
            "hoi3.country_history.country_unresolved")
        && HasDiagnostic(
            diagnostics,
            "hoi3.country_history.orphan_right_brace_ignored")
        && HasDiagnostic(
            diagnostics,
            "hoi3.country_history.patch_missing_right_brace")
        && HasDiagnostic(
            diagnostics,
            "hoi3.country_history.flag_whitespace_recovered")
        && definitions.CountryHistories().Append({}, {})
            == dillen::compatibility::hoi3::content::CountryHistoryAppendResult::Frozen;

    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    if (!valid)
    {
        std::cerr
            << "countries=" << definitions.CountryHistories().Size()
            << " sources=" << definitions.CountryHistories().SourceCount()
            << " patches=" << patchCount
            << " china=" << (china != nullptr)
            << " capital=" << (capital == nullptr ? 0 : capital->value)
            << " alliance=" << (allianceCountry != nullptr)
            << '\n';
        std::cerr << "Country history Registry validation failed\n";
        return 5;
    }

    std::cout
        << "Country history slice: passed ("
        << definitions.CountryHistories().SourceCount()
        << " sources, "
        << definitions.CountryHistories().Size()
        << " Timelines, "
        << patchCount
        << " dated patches)\n";
    return 0;
}
