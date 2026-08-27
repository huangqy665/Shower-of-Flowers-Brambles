#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "analyzer.hpp"
#include "country_tag_slice.hpp"
#include "definition_registry.hpp"
#include "diplomacy_history_slice.hpp"
#include "file_catalog.hpp"
#include "parser_registry.hpp"
#include "template_registry.hpp"
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

bool CopyFixture(const std::filesystem::path& root)
{
    const std::filesystem::path repository =
        std::filesystem::current_path();
    if (!CopyFile(
            repository / "common/countries.txt",
            root / "common/countries.txt"))
    {
        return false;
    }
    std::error_code error;
    for (std::filesystem::directory_iterator iterator(
            repository / "history/diplomacy", error), end;
        iterator != end && !error;
        iterator.increment(error))
    {
        if (iterator->is_regular_file(error)
            && !CopyFile(
                iterator->path(),
                root / "history/diplomacy"
                    / iterator->path().filename()))
        {
            return false;
        }
        error.clear();
    }
    return !error;
}

bool HasRelation(
    const dillen::worldbuilder::AuthoritativeWorld& world,
    dillen::content::DiplomaticRelationKind kind,
    dillen::content::CountryDefinitionId first,
    dillen::content::CountryDefinitionId second
)
{
    const auto key = dillen::content::CanonicalDiplomacyHistoryKey(
        kind,
        first,
        second
    );
    for (const auto& relation : world.Relations())
    {
        if (relation.kind == key.kind
            && relation.first == key.first
            && relation.second == key.second)
        {
            return true;
        }
    }
    return false;
}

}

int main()
{
    namespace fs = std::filesystem;
    using namespace dillen;
    const fs::path root = fs::temp_directory_path()
        / ("project_dillen_diplomacy_"
            + std::to_string(
                std::chrono::steady_clock::now()
                    .time_since_epoch()
                    .count()
            ));
    if (!CopyFixture(root))
    {
        std::cerr << "Diplomacy fixture creation failed\n";
        return 1;
    }

    parser::TemplateRegistry templates;
    parser::ParserRegistry parsers;
    parser::Analyzer analyzer;
    content::DefinitionRegistry definitions;
    if (!parser::hoi3::RegisterCountryTagSlice(
            templates, parsers, analyzer, definitions)
        || !parser::hoi3::RegisterDiplomacyHistorySlice(
            templates, parsers, analyzer, definitions))
    {
        std::cerr << "Diplomacy pipeline registration failed\n";
        return 2;
    }
    templates.Freeze();
    parsers.Freeze();
    analyzer.Freeze();

    parser::DiagnosticBag diagnostics;
    parser::FileCatalog catalog;
    parser::AnalysisWorkspace workspace;
    if (!catalog.AddLayer({1, "fixture", root, 0, {}})
        || !catalog.Build(templates, diagnostics)
        || !analyzer.Analyze(
            catalog,
            parsers,
            workspace,
            diagnostics))
    {
        std::cerr << "Diplomacy pipeline analysis failed\n";
        return 3;
    }

    std::size_t alliances = 0;
    std::size_t guarantees = 0;
    std::size_t vassals = 0;
    for (const content::DiplomacyHistoryTimeline& timeline
        : definitions.DiplomacyHistories().All())
    {
        if (timeline.key.kind == content::DiplomaticRelationKind::Alliance)
        {
            ++alliances;
        }
        else if (timeline.key.kind
            == content::DiplomaticRelationKind::Guarantee)
        {
            ++guarantees;
        }
        else
        {
            ++vassals;
        }
    }
    if (definitions.DiplomacyHistories().Size() != 107
        || definitions.DiplomacyHistories().PeriodCount() != 114
        || alliances != 45
        || guarantees != 36
        || vassals != 26)
    {
        std::cerr << "Diplomacy Registry counts mismatch\n";
        return 4;
    }

    definitions.Freeze();
    const auto sov = content::CountryTag::Parse("SOV")->StableId();
    const auto tan = content::CountryTag::Parse("TAN")->StableId();
    const auto eng = content::CountryTag::Parse("ENG")->StableId();
    const auto bel = content::CountryTag::Parse("BEL")->StableId();
    const auto cgd = content::CountryTag::Parse("CGD")->StableId();
    const auto cgx = content::CountryTag::Parse("CGX")->StableId();
    const auto* duplicateTimeline = definitions.DiplomacyHistories().Find({
        content::DiplomaticRelationKind::Alliance,
        cgd,
        cgx
    });
    if (duplicateTimeline == nullptr
        || duplicateTimeline->periods.size() != 2)
    {
        std::cerr << "Diplomacy duplicate timeline mismatch\n";
        return 5;
    }

    worldbuilder::WorldBuilder builder;
    worldbuilder::AuthoritativeWorld world;
    worldbuilder::WorldBuildReport report;
    if (!builder.Build(definitions, {1936, 1, 1}, world, report))
    {
        std::cerr << "Diplomacy world construction failed\n";
        return 6;
    }
    std::size_t activeAlliances = 0;
    std::size_t activeGuarantees = 0;
    std::size_t activeVassals = 0;
    for (const auto& relation : world.Relations())
    {
        if (relation.kind == content::DiplomaticRelationKind::Alliance)
        {
            ++activeAlliances;
        }
        else if (relation.kind
            == content::DiplomaticRelationKind::Guarantee)
        {
            ++activeGuarantees;
        }
        else
        {
            ++activeVassals;
        }
    }
    const auto* sovietUnion = world.FindCountry(sov);
    const auto* tannuTuva = world.FindCountry(tan);
    const auto* unitedKingdom = world.FindCountry(eng);
    const auto* belgium = world.FindCountry(bel);
    if (world.Relations().size() != 44
        || activeAlliances != 10
        || activeGuarantees != 23
        || activeVassals != 11
        || sovietUnion == nullptr
        || tannuTuva == nullptr
        || unitedKingdom == nullptr
        || belgium == nullptr
        || sovietUnion->alliances.count(tan) != 1
        || tannuTuva->alliances.count(sov) != 1
        || sovietUnion->subjects.count(tan) != 1
        || tannuTuva->overlord != sov
        || unitedKingdom->guarantees.count(bel) != 1
        || belgium->guaranteedBy.count(eng) != 1)
    {
        std::cerr << "Diplomacy runtime graph mismatch\n";
        return 7;
    }

    if (!builder.Build(definitions, {1936, 9, 1}, world, report)
        || HasRelation(
            world,
            content::DiplomaticRelationKind::Alliance,
            cgd,
            cgx))
    {
        std::cerr << "Diplomacy end-date semantics mismatch\n";
        return 8;
    }

    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    std::cout
        << "Diplomacy history: passed (107 timelines, 114 periods, "
        << world.Relations().size() << " active edges)\n";
    return 0;
}
