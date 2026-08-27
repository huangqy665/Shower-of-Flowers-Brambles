#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "analyzer.hpp"
#include "definition_registry.hpp"
#include "diagnostic.hpp"
#include "file_catalog.hpp"
#include "parser_registry.hpp"
#include "province_definition_parser.hpp"
#include "province_definition_slice.hpp"
#include "region_definition.hpp"
#include "region_definition_slice.hpp"
#include "template_registry.hpp"

namespace
{

bool CopyMapData(const std::filesystem::path& root)
{
    namespace fs = std::filesystem;
    std::error_code error;
    fs::create_directories(root / "map", error);
    if (error)
    {
        return false;
    }
    for (const char* name : {"definition.csv", "region.txt"})
    {
        fs::copy_file(
            fs::current_path() / "map" / name,
            root / "map" / name,
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

bool HasProvince(
    const dillen::content::RegionDefinition& region,
    std::uint32_t provinceId
)
{
    return std::any_of(
        region.provinces.begin(),
        region.provinces.end(),
        [provinceId](dillen::content::ProvinceDefinitionId id)
        {
            return id.value == provinceId;
        }
    );
}

bool ValidateMissingProvinceDiagnostic()
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path()
        / ("project_dillen_missing_province_"
            + std::to_string(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()
            ));
    if (!WriteText(
            root / "map/definition.csv",
            "province;red;green;blue;x;x\n"
            "1;1;2;3;\"one;province\";x\n")
        || !WriteText(
            root / "map/region.txt",
            "invalid_region = { 2 }\n"))
    {
        return false;
    }

    dillen::parser::TemplateRegistry templates;
    dillen::parser::ParserRegistry parsers;
    dillen::parser::Analyzer analyzer;
    dillen::content::DefinitionRegistry definitions;
    if (!dillen::parser::hoi3::RegisterProvinceDefinitionSlice(
            templates,
            parsers,
            analyzer,
            definitions)
        || !dillen::parser::hoi3::RegisterRegionDefinitionSlice(
            templates,
            parsers,
            analyzer,
            definitions))
    {
        return false;
    }
    templates.Freeze();
    parsers.Freeze();
    analyzer.Freeze();

    dillen::parser::DiagnosticBag diagnostics;
    dillen::parser::FileCatalog catalog;
    dillen::parser::AnalysisWorkspace workspace;
    const bool analyzed = catalog.AddLayer({1, "fixture", root, 0, {}})
        && catalog.Build(templates, diagnostics)
        && !analyzer.Analyze(
            catalog,
            parsers,
            workspace,
            diagnostics);
    const auto* province = definitions.Provinces().Find(1);
    const bool valid = analyzed
        && HasDiagnostic(
            diagnostics,
            "hoi3.region.province_missing"
        )
        && definitions.Provinces().Size() == 1
        && province != nullptr
        && province->name == "one;province"
        && definitions.Regions().Size() == 1
        && definitions.Regions().ResolvedCount() == 0;
    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    return valid;
}

}

int main()
{
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path()
        / ("project_dillen_province_region_"
            + std::to_string(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()
            ));
    if (!CopyMapData(root))
    {
        std::cerr << "Province/Region fixture creation failed\n";
        return 1;
    }

    dillen::parser::TemplateRegistry templates;
    dillen::parser::ParserRegistry parsers;
    dillen::parser::Analyzer analyzer;
    dillen::content::DefinitionRegistry definitions;
    if (!dillen::parser::hoi3::RegisterProvinceDefinitionSlice(
            templates,
            parsers,
            analyzer,
            definitions)
        || !dillen::parser::hoi3::RegisterRegionDefinitionSlice(
            templates,
            parsers,
            analyzer,
            definitions))
    {
        std::cerr << "Province/Region slice registration failed\n";
        return 2;
    }
    templates.Freeze();
    parsers.Freeze();
    analyzer.Freeze();

    dillen::parser::DiagnosticBag diagnostics;
    dillen::parser::FileCatalog catalog;
    if (!catalog.AddLayer({1, "repository", root, 0, {}})
        || !catalog.Build(templates, diagnostics)
        || catalog.ActiveClassifiedFileCount() != 2)
    {
        std::cerr << "Province/Region catalog failed\n";
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
        std::cerr << "Province/Region analysis failed\n";
        return 4;
    }

    const dillen::parser::hoi3::ProvinceDefinitionDocument* csvDocument =
        nullptr;
    for (const auto& file : workspace.files)
    {
        if (file.result.artifact.type
            == dillen::parser::hoi3::kProvinceDefinitionDocumentType)
        {
            csvDocument = file.result.artifact.As<
                dillen::parser::hoi3::ProvinceDefinitionDocument>();
        }
    }
    definitions.Freeze();

    const auto* province1 = definitions.Provinces().Find(1);
    const auto* quotedProvince = definitions.Provinces().Find(11576);
    const auto* colorProvince = definitions.Provinces().FindByColor(
        {42, 3, 128}
    );
    const auto* guangdong = definitions.Regions().Find(
        "guangdong_region"
    );
    const auto* finland = definitions.Regions().Find(
        "finland_etela_kannas"
    );
    const auto* china = definitions.Regions().Find("SF_china_region");
    const auto* manchuria = definitions.Regions().Find(
        "manchu_china_region"
    );
    const bool valid = ValidateMissingProvinceDiagnostic()
        && csvDocument != nullptr
        && csvDocument->definitions.size() == 14187
        && csvDocument->paletteRowCount == 57
        && csvDocument->compatibilityWrappedRowCount == 24
        && definitions.Provinces().Size() == 14187
        && province1 != nullptr
        && province1->color
            == dillen::content::ProvinceColor{42, 3, 128}
        && colorProvince == province1
        && quotedProvince != nullptr
        && quotedProvince->name == "pacific ocean"
        && definitions.Regions().Size() == 2250
        && definitions.Regions().ResolvedCount() == 2250
        && guangdong != nullptr
        && guangdong->provinces.size() == 56
        && guangdong->HasFlag("peace")
        && HasProvince(*guangdong, 14184)
        && finland != nullptr
        && finland->HasFlag("ai_prio")
        && china != nullptr
        && china->provinces.size() == 748
        && HasProvince(*china, 4787)
        && manchuria != nullptr
        && HasProvince(*manchuria, 4787)
        && HasDiagnostic(
            diagnostics,
            "hoi3.region.province_duplicate"
        )
        && definitions.Regions().Find(
            dillen::content::StableRegionDefinitionId(
                "guangdong_region"
            )
        ) == guangdong
        && definitions.Provinces().Declare({})
            == dillen::content::ProvinceDeclareResult::Frozen
        && definitions.Regions().Resolve({}, {}, {})
            == dillen::content::RegionResolveResult::Frozen;

    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    if (!valid)
    {
        std::cerr
            << "csv="
            << (csvDocument == nullptr
                ? 0
                : csvDocument->definitions.size())
            << " palette="
            << (csvDocument == nullptr
                ? 0
                : csvDocument->paletteRowCount)
            << " wrapped="
            << (csvDocument == nullptr
                ? 0
                : csvDocument->compatibilityWrappedRowCount)
            << " provinces=" << definitions.Provinces().Size()
            << " regions=" << definitions.Regions().Size()
            << " resolved=" << definitions.Regions().ResolvedCount()
            << " quoted_name="
            << (quotedProvince == nullptr
                ? "<missing>"
                : quotedProvince->name)
            << " guangdong="
            << (guangdong == nullptr ? 0 : guangdong->provinces.size())
            << " china="
            << (china == nullptr ? 0 : china->provinces.size())
            << " duplicate_warning="
            << HasDiagnostic(
                diagnostics,
                "hoi3.region.province_duplicate"
            )
            << '\n';
        std::cerr << "Province/Region Registry validation failed\n";
        return 5;
    }

    std::cout
        << "Province/Region slice: passed ("
        << definitions.Provinces().Size()
        << " Provinces, "
        << definitions.Regions().Size()
        << " Regions)\n";
    return 0;
}
