#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

#include "resolver.hpp"
#include "country_tag_slice.hpp"
#include "definition_registry.hpp"
#include "diplomacy_history_slice.hpp"
#include "file_catalog.hpp"
#include "parser_registry.hpp"
#include "template_registry.hpp"
#include "war_history_slice.hpp"
#include "world_builder.hpp"

namespace {

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

bool CopyDirectoryFiles(
    const std::filesystem::path& source,
    const std::filesystem::path& destination
)
{
    std::error_code error;
    for (std::filesystem::directory_iterator iterator(source, error), end;
        iterator != end && !error;
        iterator.increment(error))
    {
        if (iterator->is_regular_file(error)
            && !CopyFile(
                iterator->path(),
                destination / iterator->path().filename()))
        {
            return false;
        }
        error.clear();
    }
    return !error;
}

bool CopyFixture(const std::filesystem::path& root)
{
    const std::filesystem::path repository =
        std::filesystem::current_path();
    return CopyFile(
            repository / "common/countries.txt",
            root / "common/countries.txt")
        && CopyDirectoryFiles(
            repository / "history/diplomacy",
            root / "history/diplomacy")
        && CopyDirectoryFiles(
            repository / "history/wars",
            root / "history/wars");
}

void PrintBuildErrors(const dillen::compatibility::hoi3::worldbuilder::WorldBuildReport& report)
{
    for (const auto& issue : report.All())
    {
        if (issue.severity
            == dillen::compatibility::hoi3::worldbuilder::WorldBuildIssueSeverity::Error)
        {
            std::cerr << issue.code << ": " << issue.message << '\n';
        }
    }
}

}

int main()
{
    namespace fs = std::filesystem;
    using namespace dillen;
    const fs::path root = fs::temp_directory_path()
        / ("project_dillen_wars_"
            + std::to_string(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()
            ));
    if (!CopyFixture(root))
    {
        std::cerr << "War fixture creation failed\n";
        return 1;
    }

    parser::TemplateRegistry templates;
    parser::ParserRegistry parsers;
    parser::Resolver resolver;
    dillen::compatibility::hoi3::content::DefinitionRegistry definitions;
    if (!parser::hoi3::RegisterCountryTagSlice(
            templates, parsers, resolver, definitions)
        || !parser::hoi3::RegisterDiplomacyHistorySlice(
            templates, parsers, resolver, definitions)
        || !parser::hoi3::RegisterWarHistorySlice(
            templates, parsers, resolver, definitions))
    {
        std::cerr << "War pipeline registration failed\n";
        return 2;
    }
    templates.Freeze();
    parsers.Freeze();
    resolver.Freeze();

    parser::DiagnosticBag diagnostics;
    parser::FileCatalog catalog;
    parser::ParseWorkspace workspace;
    if (!catalog.AddLayer({1, "fixture", root, 0, {}})
        || !catalog.Build(templates, diagnostics)
        || !(catalog.Parse(parsers, workspace, diagnostics) && resolver.Resolve(workspace, diagnostics)))
    {
        std::cerr << "War pipeline analysis failed\n";
        return 3;
    }

    const auto& wars = definitions.WarHistories();
    if (wars.Size() != 6
        || wars.PatchCount() != 33
        || wars.ParticipantOperationCount() != 101
        || wars.WarGoalCount() != 39)
    {
        std::cerr << "War Registry counts mismatch\n";
        return 4;
    }
    const dillen::compatibility::hoi3::content::WarHistoryTimeline* sino = wars.Find(
        "history/wars/sinojapanesewar.txt"
    );
    const dillen::compatibility::hoi3::content::WarHistoryTimeline* civil = wars.Find(
        "history/wars/chinesecivilwar.txt"
    );
    if (sino == nullptr
        || civil == nullptr
        || !sino->limitedWar
        || sino->patches.size() != 9
        || civil->patches.size() != 11)
    {
        std::cerr << "War timeline lookup mismatch\n";
        return 5;
    }

    definitions.Freeze();
    civil = definitions.WarHistories().Find(
        "history/wars/chinesecivilwar.txt"
    );
    if (civil == nullptr
        || civil->patches[7].date != dillen::compatibility::hoi3::content::DefinitionDate{1949, 9, 1}
        || civil->patches[10].date != dillen::compatibility::hoi3::content::DefinitionDate{1950, 5, 1})
    {
        std::cerr << "Partial war date normalization mismatch\n";
        return 6;
    }

    compatibility::hoi3::worldbuilder::WorldBuilder builder;
    compatibility::hoi3::worldbuilder::Hoi3WorldState world;
    compatibility::hoi3::worldbuilder::WorldBuildReport report;
    if (!builder.Build(definitions, {1936, 1, 1}, world, report))
    {
        PrintBuildErrors(report);
        std::cerr << "War world construction failed\n";
        return 7;
    }

    const auto jap = dillen::compatibility::hoi3::content::CountryTag::Parse("JAP")->StableId();
    const auto man = dillen::compatibility::hoi3::content::CountryTag::Parse("MAN")->StableId();
    const auto chi = dillen::compatibility::hoi3::content::CountryTag::Parse("CHI")->StableId();
    const auto chc = dillen::compatibility::hoi3::content::CountryTag::Parse("CHC")->StableId();
    const auto cdb = dillen::compatibility::hoi3::content::CountryTag::Parse("CDB")->StableId();
    const auto sinoId = dillen::compatibility::hoi3::content::StableWarHistoryDefinitionId(
        "history/wars/sinojapanesewar.txt"
    );
    const auto civilId = dillen::compatibility::hoi3::content::StableWarHistoryDefinitionId(
        "history/wars/chinesecivilwar.txt"
    );
    const compatibility::hoi3::worldbuilder::RuntimeWarState* sinoWar = world.FindWar(sinoId);
    const compatibility::hoi3::worldbuilder::RuntimeWarState* civilWar = world.FindWar(civilId);
    const compatibility::hoi3::worldbuilder::CountryState* japan = world.FindCountry(jap);
    const compatibility::hoi3::worldbuilder::CountryState* communists = world.FindCountry(chc);
    if (world.Wars().size() != 2
        || sinoWar == nullptr
        || civilWar == nullptr
        || sinoWar->attackers.count(jap) != 1
        || sinoWar->attackers.count(man) != 1
        || sinoWar->defenders.count(chc) != 1
        || civilWar->attackers.count(chi) != 1
        || civilWar->attackers.count(cdb) != 1
        || civilWar->defenders.count(chc) != 1
        || japan == nullptr
        || communists == nullptr
        || japan->offensiveWars.count(sinoId) != 1
        || communists->defensiveWars.count(sinoId) != 1
        || communists->defensiveWars.count(civilId) != 1)
    {
        std::cerr << "Runtime War graph mismatch\n";
        return 8;
    }

    if (!builder.Build(definitions, {1937, 8, 1}, world, report))
    {
        PrintBuildErrors(report);
        std::cerr << "War diplomacy validation failed\n";
        return 9;
    }
    sinoWar = world.FindWar(sinoId);
    if (world.Wars().size() != 2
        || sinoWar == nullptr
        || sinoWar->attackers.size() != 4
        || sinoWar->defenders.size() != 12
        || sinoWar->warGoals.size() != 9)
    {
        std::cerr << "Dated War replay mismatch\n";
        return 10;
    }

    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    std::cout
        << "War history: passed (6 timelines, 33 patches, "
        << world.Wars().size() << " active wars)\n";
    return 0;
}
