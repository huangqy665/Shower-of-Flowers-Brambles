#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include "analyzer.hpp"
#include "country_history_slice.hpp"
#include "country_tag_slice.hpp"
#include "definition_registry.hpp"
#include "diagnostic.hpp"
#include "file_catalog.hpp"
#include "launch_slice.hpp"
#include "order_of_battle_slice.hpp"
#include "parser_registry.hpp"
#include "province_definition_slice.hpp"
#include "province_history_slice.hpp"
#include "scenario_overlay.hpp"
#include "template_registry.hpp"
#include "world_builder.hpp"

namespace {

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

bool CreateFixture(
    const std::filesystem::path& base,
    const std::filesystem::path& mod
)
{
    return WriteText(
            base / "common/bookmarks.txt",
            "bookmark = {\n"
            " name = BASE_BOOKMARK\n"
            " date = 1933.1.1\n"
            " country = CHI\n"
            "}\n")
        && WriteText(
            mod / "common/bookmarks.txt",
            "bookmark = {\n"
            " name = MOD_BOOKMARK\n"
            " date = 1936.1.1\n"
            " country = CHI\n"
            "}\n")
        && WriteText(
            mod / "common/countries.txt",
            "CHI = \"countries/China.txt\"\n"
            "JAP = \"countries/Japan.txt\"\n")
        && WriteText(
            mod / "map/definition.csv",
            "province;red;green;blue;x;x\n"
            "1;1;2;3;Test Province;x\n")
        && WriteText(
            mod / "history/countries/CHI - Base.txt",
            "capital = 1\n"
            "government = base_government\n")
        && WriteText(
            mod / "history/provinces/1 - Base.txt",
            "owner = CHI\n"
            "controller = CHI\n"
            "add_core = CHI\n"
            "infra = 2\n")
        && WriteText(
            mod / "scenarios/Probe Scenario.txt",
            "name = PROBE_SCENARIO_NAME\n"
            "desc = PROBE_SCENARIO_DESC\n"
            "icon = GFX_PROBE\n"
            "startdate = 1940.1.2\n"
            "enddate = 1940.12.31\n"
            "selectable_country = CHI\n"
            "country = JAP\n"
            "camera_center = { x = 1 y = 2 }\n"
            "provinces = { 1 }\n")
        && WriteText(
            mod / "scenarios/Probe_Scenario/CHI.txt",
            "capital = 1\n"
            "government = scenario_government\n"
            "oob = \"CHI_OOB.txt\"\n")
        && WriteText(
            mod / "scenarios/Probe_Scenario/CHI_OOB.txt",
            "division = {\n"
            " name = \"Scenario Division\"\n"
            " location = 1\n"
            " regiment = { type = infantry_brigade }\n"
            "}\n")
        && WriteText(
            mod / "scenarios/Probe_Scenario/Provinces/1 - Overlay.txt",
            "owner = CHI\n"
            "controller = JAP\n"
            "industry = 4\n");
}

void PrintDiagnostics(
    const dillen::parser::AnalysisWorkspace& workspace,
    const dillen::parser::DiagnosticBag& diagnostics
)
{
    for (const auto& diagnostic : diagnostics.All())
    {
        if (diagnostic.severity != dillen::parser::DiagnosticSeverity::Error
            && diagnostic.severity
                != dillen::parser::DiagnosticSeverity::Fatal)
        {
            continue;
        }
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

bool RegisterMetadataPipeline(
    dillen::parser::TemplateRegistry& templates,
    dillen::parser::ParserRegistry& parsers,
    dillen::parser::Analyzer& analyzer,
    dillen::content::DefinitionRegistry& definitions
)
{
    return dillen::parser::hoi3::RegisterCountryTagSlice(
            templates, parsers, analyzer, definitions)
        && dillen::parser::hoi3::RegisterLaunchDefinitionSlice(
            templates, parsers, analyzer, definitions);
}

bool RegisterContentPipeline(
    dillen::parser::TemplateRegistry& templates,
    dillen::parser::ParserRegistry& parsers,
    dillen::parser::Analyzer& analyzer,
    dillen::content::DefinitionRegistry& definitions
)
{
    return RegisterMetadataPipeline(templates, parsers, analyzer, definitions)
        && dillen::parser::hoi3::RegisterProvinceDefinitionSlice(
            templates, parsers, analyzer, definitions)
        && dillen::parser::hoi3::RegisterCountryHistorySlice(
            templates, parsers, analyzer, definitions)
        && dillen::parser::hoi3::RegisterProvinceHistorySlice(
            templates, parsers, analyzer, definitions)
        && dillen::parser::hoi3::RegisterOrderOfBattleSlice(
            templates, parsers, analyzer, definitions);
}

bool AddBaseAndModLayers(
    dillen::parser::FileCatalog& catalog,
    const std::filesystem::path& base,
    const std::filesystem::path& mod
)
{
    return catalog.AddLayer({1, "base", base, 0, {}})
        && catalog.AddLayer({2, "mod", mod, 100, {}});
}

bool HasActivePath(
    const dillen::parser::FileCatalog& catalog,
    const std::string& path
)
{
    for (const auto& file : catalog.Files())
    {
        if (file.virtualPath == path
            && file.disposition == dillen::parser::CatalogDisposition::Active)
        {
            return true;
        }
    }
    return false;
}

bool ValidateRepositoryLaunchMetadata()
{
    using namespace dillen;
    parser::TemplateRegistry templates;
    parser::ParserRegistry parsers;
    parser::Analyzer analyzer;
    content::DefinitionRegistry definitions;
    if (!RegisterMetadataPipeline(templates, parsers, analyzer, definitions))
    {
        return false;
    }
    templates.Freeze();
    parsers.Freeze();
    analyzer.Freeze();

    parser::SourceLayer repository;
    repository.id = 1;
    repository.name = "repository";
    repository.root = std::filesystem::current_path();
    repository.includePatterns = {
        "common/bookmarks.txt",
        "common/countries.txt",
        "scenarios/*.txt"
    };
    parser::DiagnosticBag diagnostics;
    parser::FileCatalog catalog;
    parser::AnalysisWorkspace workspace;
    if (!catalog.AddLayer(std::move(repository))
        || !catalog.Build(templates, diagnostics)
        || !analyzer.Analyze(catalog, parsers, workspace, diagnostics))
    {
        PrintDiagnostics(workspace, diagnostics);
        return false;
    }
    definitions.Freeze();
    if (definitions.Launches().BookmarkCount() != 6
        || definitions.Launches().ScenarioCount() != 14)
    {
        return false;
    }

    parser::SourceLayerId layerId = 100;
    for (const content::ScenarioDefinition& scenario
        : definitions.Launches().Scenarios())
    {
        parser::hoi3::ScenarioOverlayPlan overlay;
        if (!parser::hoi3::BuildScenarioOverlayPlan(
                scenario,
                std::filesystem::current_path(),
                layerId,
                1000,
                overlay,
                diagnostics)
            || overlay.mounts.size() < 2
            || overlay.mounts.size() > 3)
        {
            return false;
        }
        layerId += 3;
    }
    const content::ScenarioDefinition* dny =
        definitions.Launches().FindScenario("DNY");
    return dny != nullptr
        && dny->startDate == content::DefinitionDate{1942, 3, 1};
}

}

int main()
{
    namespace fs = std::filesystem;
    using namespace dillen;
    const fs::path root = fs::temp_directory_path()
        / ("project_dillen_scenario_launch_"
            + std::to_string(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()
            ));
    const fs::path base = root / "base";
    const fs::path mod = root / "mod";
    if (!ValidateRepositoryLaunchMetadata())
    {
        std::cerr << "Repository launch metadata validation failed\n";
        return 1;
    }
    if (!CreateFixture(base, mod))
    {
        std::cerr << "Scenario launch fixture creation failed\n";
        return 2;
    }

    parser::TemplateRegistry metadataTemplates;
    parser::ParserRegistry metadataParsers;
    parser::Analyzer metadataAnalyzer;
    content::DefinitionRegistry metadataDefinitions;
    if (!RegisterMetadataPipeline(
            metadataTemplates,
            metadataParsers,
            metadataAnalyzer,
            metadataDefinitions))
    {
        std::cerr << "Scenario metadata pipeline registration failed\n";
        return 2;
    }
    metadataTemplates.Freeze();
    metadataParsers.Freeze();
    metadataAnalyzer.Freeze();
    parser::DiagnosticBag metadataDiagnostics;
    parser::FileCatalog metadataCatalog;
    parser::AnalysisWorkspace metadataWorkspace;
    if (!AddBaseAndModLayers(metadataCatalog, base, mod)
        || !metadataCatalog.Build(metadataTemplates, metadataDiagnostics)
        || !metadataAnalyzer.Analyze(
            metadataCatalog,
            metadataParsers,
            metadataWorkspace,
            metadataDiagnostics))
    {
        PrintDiagnostics(metadataWorkspace, metadataDiagnostics);
        std::cerr << "Scenario metadata compilation failed\n";
        return 3;
    }
    metadataDefinitions.Freeze();
    const content::BookmarkDefinition* bookmark =
        metadataDefinitions.Launches().FindBookmark("mod_bookmark");
    const content::ScenarioDefinition* scenario =
        metadataDefinitions.Launches().FindScenario("probe scenario");
    if (metadataDefinitions.Launches().BookmarkCount() != 1
        || metadataDefinitions.Launches().ScenarioCount() != 1
        || bookmark == nullptr
        || bookmark->date != content::DefinitionDate{1936, 1, 1}
        || scenario == nullptr
        || scenario->startDate != content::DefinitionDate{1940, 1, 2})
    {
        std::cerr << "Scenario metadata selection mismatch\n";
        return 4;
    }

    parser::hoi3::ScenarioOverlayPlan overlay;
    if (!parser::hoi3::BuildScenarioOverlayPlan(
            *scenario,
            mod,
            10,
            200,
            overlay,
            metadataDiagnostics)
        || overlay.mounts.size() != 3)
    {
        std::cerr << "Scenario overlay planning failed\n";
        return 5;
    }

    parser::TemplateRegistry templates;
    parser::ParserRegistry parsers;
    parser::Analyzer analyzer;
    content::DefinitionRegistry definitions;
    if (!RegisterContentPipeline(templates, parsers, analyzer, definitions))
    {
        std::cerr << "Scenario content pipeline registration failed\n";
        return 6;
    }
    templates.Freeze();
    parsers.Freeze();
    analyzer.Freeze();
    parser::DiagnosticBag diagnostics;
    parser::FileCatalog catalog;
    if (!AddBaseAndModLayers(catalog, base, mod))
    {
        return 7;
    }
    for (const parser::SourceLayer& mount : overlay.mounts)
    {
        if (!catalog.AddLayer(mount))
        {
            std::cerr << "Scenario overlay mount failed\n";
            return 7;
        }
    }
    parser::AnalysisWorkspace workspace;
    if (!catalog.Build(templates, diagnostics)
        || !HasActivePath(catalog, "history/countries/chi.txt")
        || !HasActivePath(
            catalog,
            "history/provinces/1 - overlay.txt")
        || !HasActivePath(catalog, "history/units/chi_oob.txt")
        || !analyzer.Analyze(catalog, parsers, workspace, diagnostics))
    {
        PrintDiagnostics(workspace, diagnostics);
        std::cerr << "Scenario content compilation failed\n";
        return 8;
    }
    definitions.Freeze();

    const content::ScenarioDefinition* compiledScenario =
        definitions.Launches().FindScenario("probe_scenario");
    worldbuilder::WorldBuilder builder;
    worldbuilder::AuthoritativeWorld world;
    worldbuilder::WorldBuildReport report;
    if (compiledScenario == nullptr
        || !builder.BuildScenario(
            definitions,
            compiledScenario->id,
            world,
            report))
    {
        std::cerr << "Scenario world construction failed\n";
        return 9;
    }
    const auto* china = world.FindCountry("CHI");
    const auto* japan = world.FindCountry("JAP");
    const auto* province = world.FindProvince(1);
    const auto industry = province == nullptr
        ? std::nullopt
        : province->Numeric(content::ProvinceHistoryField::Industry);
    const auto* division = china == nullptr
            || china->unitRoots.size() != 1
        ? nullptr
        : world.FindUnit(china->unitRoots.front());
    const auto* regiment = division == nullptr
            || division->children.size() != 1
        ? nullptr
        : world.FindUnit(division->children.front());
    if (china == nullptr
        || japan == nullptr
        || province == nullptr
        || world.Units().size() != 2
        || division == nullptr
        || regiment == nullptr
        || world.Date() != content::DefinitionDate{1940, 1, 2}
        || world.Scenario() != compiledScenario->id
        || world.Bookmark().has_value()
        || china->government != "scenario_government"
        || china->ordersOfBattle.size() != 1
        || !china->ordersOfBattle.front().definition
        || division->kind
            != content::OrderOfBattleNodeKind::Division
        || division->name != "Scenario Division"
        || division->country != china->id
        || division->location != content::ProvinceDefinitionId{1}
        || division->parent.has_value()
        || regiment->kind
            != content::OrderOfBattleNodeKind::Regiment
        || regiment->country != china->id
        || regiment->location != content::ProvinceDefinitionId{1}
        || regiment->parent != division->id
        || province->locatedUnits.size() != 2
        || china->ownedProvinces
            != std::vector<content::ProvinceDefinitionId>{{1}}
        || china->controlledProvinces.size() != 0
        || china->coreProvinces
            != std::vector<content::ProvinceDefinitionId>{{1}}
        || japan->ownedProvinces.size() != 0
        || japan->controlledProvinces
            != std::vector<content::ProvinceDefinitionId>{{1}}
        || japan->coreProvinces.size() != 0
        || province->owner != china->id
        || province->controller != japan->id
        || province->cores.count(china->id) != 1
        || !industry
        || *industry != 4.0)
    {
        std::cerr << "Scenario world state mismatch\n";
        return 10;
    }

    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    std::cout
        << "Scenario launch pipeline: passed (bookmark override, 3 mounts, "
        << world.Countries().size() << " countries)\n";
    return 0;
}
