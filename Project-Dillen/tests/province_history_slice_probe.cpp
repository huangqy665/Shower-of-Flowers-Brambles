#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

#include "resolver.hpp"
#include "country_tag_definition.hpp"
#include "country_tag_slice.hpp"
#include "definition_registry.hpp"
#include "diagnostic.hpp"
#include "file_catalog.hpp"
#include "parser_registry.hpp"
#include "province_definition_slice.hpp"
#include "province_history.hpp"
#include "province_history_registry.hpp"
#include "province_history_slice.hpp"
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
            repository / "history/provinces",
            root / "history/provinces");
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

dillen::compatibility::hoi3::content::CountryDefinitionId CountryId(const char* text)
{
    const auto tag = dillen::compatibility::hoi3::content::CountryTag::Parse(text);
    return tag ? tag->StableId() : dillen::compatibility::hoi3::content::CountryDefinitionId{};
}

bool HasCountryOperation(
    const std::vector<dillen::compatibility::hoi3::content::ProvinceHistoryOperation>& operations,
    dillen::compatibility::hoi3::content::ProvinceHistoryField field,
    const char* tag
)
{
    const dillen::compatibility::hoi3::content::CountryDefinitionId expected = CountryId(tag);
    return std::any_of(
        operations.begin(),
        operations.end(),
        [field, expected](
            const dillen::compatibility::hoi3::content::ProvinceHistoryOperation& operation)
        {
            const auto* value = std::get_if<
                dillen::compatibility::hoi3::content::CountryDefinitionId>(&operation.value);
            return operation.field == field
                && value != nullptr
                && *value == expected;
        }
    );
}

bool HasIntegerOperation(
    const std::vector<dillen::compatibility::hoi3::content::ProvinceHistoryOperation>& operations,
    dillen::compatibility::hoi3::content::ProvinceHistoryField field,
    std::int64_t expected
)
{
    return std::any_of(
        operations.begin(),
        operations.end(),
        [field, expected](
            const dillen::compatibility::hoi3::content::ProvinceHistoryOperation& operation)
        {
            const auto* value = std::get_if<std::int64_t>(&operation.value);
            return operation.field == field
                && value != nullptr
                && *value == expected;
        }
    );
}

bool ValidateMissingProvinceDiagnostic()
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path()
        / ("project_dillen_missing_province_history_"
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
            root / "history/provinces/test/2 - missing.txt",
            "infra = 3\n"))
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
        || !dillen::parser::hoi3::RegisterProvinceHistorySlice(
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
            "hoi3.province_history.province_missing")
        && definitions.Provinces().Size() == 1
        && definitions.ProvinceHistories().Size() == 0;
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
    using dillen::compatibility::hoi3::content::ProvinceHistoryField;
    const fs::path root = fs::temp_directory_path()
        / ("project_dillen_province_history_"
            + std::to_string(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()
            ));
    if (!CopyRepositoryFixture(root))
    {
        std::cerr << "Province history fixture creation failed\n";
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
        || !dillen::parser::hoi3::RegisterProvinceHistorySlice(
            templates,
            parsers,
            resolver,
            definitions))
    {
        std::cerr << "Province history slice registration failed\n";
        return 2;
    }
    templates.Freeze();
    parsers.Freeze();
    resolver.Freeze();

    dillen::parser::DiagnosticBag diagnostics;
    dillen::parser::FileCatalog catalog;
    if (!catalog.AddLayer({1, "repository", root, 0, {}})
        || !catalog.Build(templates, diagnostics)
        || catalog.ActiveClassifiedFileCount() != 9424)
    {
        std::cerr
            << "Province history catalog failed: active="
            << catalog.ActiveClassifiedFileCount()
            << '\n';
        return 3;
    }

    dillen::parser::ParseWorkspace workspace;
    if (!(catalog.Parse(parsers, workspace, diagnostics) && resolver.Resolve(workspace, diagnostics)))
    {
        PrintDiagnostics(workspace, diagnostics);
        std::cerr << "Province history analysis failed\n";
        return 4;
    }
    definitions.Freeze();

    const auto* tanghe = definitions.ProvinceHistories().Find(7487);
    const auto* bonin = definitions.ProvinceHistories().Find(14129);
    const auto* duplicate = definitions.ProvinceHistories().Find(195);
    std::size_t patchCount = 0;
    for (const auto& timeline : definitions.ProvinceHistories().All())
    {
        patchCount += timeline.patches.size();
    }

    const bool tangheValid = tanghe != nullptr
        && tanghe->sources.size() == 1
        && tanghe->initialOperations.size() == 5
        && HasCountryOperation(
            tanghe->initialOperations,
            ProvinceHistoryField::Owner,
            "CHI")
        && HasCountryOperation(
            tanghe->initialOperations,
            ProvinceHistoryField::AddCore,
            "CHC")
        && HasIntegerOperation(
            tanghe->initialOperations,
            ProvinceHistoryField::Infrastructure,
            3)
        && tanghe->patches.size() == 4
        && tanghe->patches[0].date
            == dillen::compatibility::hoi3::content::DefinitionDate{1944, 6, 19}
        && HasCountryOperation(
            tanghe->patches[0].operations,
            ProvinceHistoryField::Controller,
            "JAP")
        && tanghe->patches[1].date
            == dillen::compatibility::hoi3::content::DefinitionDate{1945, 9, 1}
        && tanghe->patches[2].date
            == dillen::compatibility::hoi3::content::DefinitionDate{1945, 9, 1}
        && tanghe->patches[1].sequence < tanghe->patches[2].sequence
        && HasIntegerOperation(
            tanghe->patches[2].operations,
            ProvinceHistoryField::Infrastructure,
            4)
        && tanghe->patches[3].date
            == dillen::compatibility::hoi3::content::DefinitionDate{1949, 10, 1}
        && HasCountryOperation(
            tanghe->patches[3].operations,
            ProvinceHistoryField::Owner,
            "PRC");
    const bool boninValid = bonin != nullptr
        && HasCountryOperation(
            bonin->initialOperations,
            ProvinceHistoryField::AddCore,
            "JAP")
        && HasIntegerOperation(
            bonin->initialOperations,
            ProvinceHistoryField::Infrastructure,
            3);
    const bool valid = ValidateMissingProvinceDiagnostic()
        && definitions.Provinces().Size() == 14187
        && definitions.ProvinceHistories().Size() == 9305
        && definitions.ProvinceHistories().SourceCount() == 9422
        && patchCount == 11878
        && tangheValid
        && boninValid
        && duplicate != nullptr
        && duplicate->sources.size() == 2
        && HasDiagnostic(
            diagnostics,
            "hoi3.province_history.filename_noncanonical")
        && HasDiagnostic(
            diagnostics,
            "hoi3.province_history.duplicate_source_merged")
        && HasDiagnostic(
            diagnostics,
            "hoi3.province_history.concatenated_assignment_recovered")
        && HasDiagnostic(
            diagnostics,
            "hoi3.province_history.orphan_right_brace_ignored")
        && HasDiagnostic(
            diagnostics,
            "hoi3.province_history.country_unresolved")
        && definitions.ProvinceHistories().Append({1}, {})
            == dillen::compatibility::hoi3::content::ProvinceHistoryAppendResult::Frozen;

    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    if (!valid)
    {
        std::cerr
            << "provinces=" << definitions.Provinces().Size()
            << " timelines=" << definitions.ProvinceHistories().Size()
            << " sources=" << definitions.ProvinceHistories().SourceCount()
            << " patches=" << patchCount
            << " tanghe=" << tangheValid
            << " bonin=" << boninValid
            << " duplicate_sources="
            << (duplicate == nullptr ? 0 : duplicate->sources.size())
            << '\n';
        std::cerr << "Province history Registry validation failed\n";
        return 5;
    }

    std::cout
        << "Province history slice: passed ("
        << definitions.ProvinceHistories().SourceCount()
        << " sources, "
        << definitions.ProvinceHistories().Size()
        << " Timelines, "
        << patchCount
        << " dated patches)\n";
    return 0;
}
