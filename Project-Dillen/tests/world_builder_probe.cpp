#include <cmath>
#include <cstdint>
#include <iostream>
#include <string>
#include <variant>
#include <vector>

#include "country_history.hpp"
#include "country_tag_definition.hpp"
#include "definition_registry.hpp"
#include "order_of_battle_definition.hpp"
#include "province_history.hpp"
#include "world_builder.hpp"

namespace {

dillen::compatibility::hoi3::content::DefinitionOrigin Origin(std::string path)
{
    return {std::move(path), "probe", 1, 1};
}

bool DeclareCountry(
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
    std::string_view tag
)
{
    const auto parsed = dillen::compatibility::hoi3::content::CountryTag::Parse(tag);
    if (!parsed)
    {
        return false;
    }
    dillen::compatibility::hoi3::content::CountryTagDefinition definition;
    definition.id = parsed->StableId();
    definition.tag = *parsed;
    definition.declaredPath = "common/countries.txt";
    definition.definitionPath = "common/countries/" + parsed->ToString()
        + ".txt";
    definition.origin = Origin("common/countries.txt");
    return definitions.Countries().Declare(std::move(definition))
        == dillen::compatibility::hoi3::content::CountryDeclareResult::Added;
}

bool DeclareProvince(
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
    std::uint32_t id,
    dillen::compatibility::hoi3::content::ProvinceColor color
)
{
    dillen::compatibility::hoi3::content::ProvinceDefinition definition;
    definition.id = {id};
    definition.color = color;
    definition.name = "province_" + std::to_string(id);
    definition.origin = Origin("map/definition.csv");
    return definitions.Provinces().Declare(std::move(definition))
        == dillen::compatibility::hoi3::content::ProvinceDeclareResult::Added;
}

dillen::compatibility::hoi3::content::OrderOfBattleDefinitionId DeclareOrderOfBattle(
    dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
    std::string path
)
{
    path = dillen::compatibility::hoi3::content::NormalizeOrderOfBattlePath(path);
    dillen::compatibility::hoi3::content::OrderOfBattleDefinition definition;
    definition.id = dillen::compatibility::hoi3::content::StableOrderOfBattleDefinitionId(path);
    definition.virtualPath = path;
    definition.origin = Origin(path);
    const auto id = definition.id;
    if (definitions.OrdersOfBattle().Declare(std::move(definition))
        != dillen::compatibility::hoi3::content::OrderOfBattleDeclareResult::Added)
    {
        return {};
    }
    dillen::compatibility::hoi3::content::OrderOfBattleNode root;
    root.kind = dillen::compatibility::hoi3::content::OrderOfBattleNodeKind::Division;
    root.name = path;
    root.location = dillen::compatibility::hoi3::content::ProvinceDefinitionId{1};
    root.origin = Origin(path);
    if (definitions.OrdersOfBattle().ResolveReferences(
            id,
            {std::move(root)},
            {},
            {},
            {})
        != dillen::compatibility::hoi3::content::OrderOfBattleResolveResult::Resolved)
    {
        return {};
    }
    return id;
}

dillen::compatibility::hoi3::content::CountryHistoryOperation CountryOperation(
    dillen::compatibility::hoi3::content::CountryHistoryField field,
    dillen::compatibility::hoi3::content::CountryHistoryValue value,
    std::string key = {}
)
{
    return {field, std::move(key), std::move(value),
        Origin("history/countries/CHI - China.txt")};
}

dillen::compatibility::hoi3::content::ProvinceHistoryOperation ProvinceOperation(
    dillen::compatibility::hoi3::content::ProvinceHistoryField field,
    dillen::compatibility::hoi3::content::ProvinceHistoryValue value
)
{
    return {field, std::move(value),
        Origin("history/provinces/china/1 - Test.txt")};
}

bool Approximately(double first, double second)
{
    return std::abs(first - second) < 0.0001;
}

bool HasIssue(
    const dillen::compatibility::hoi3::worldbuilder::WorldBuildReport& report,
    const std::string& code
)
{
    for (const auto& issue : report.All())
    {
        if (issue.code == code)
        {
            return true;
        }
    }
    return false;
}

}

int main()
{
    using namespace dillen;
    using dillen::compatibility::hoi3::content::CountryHistoryField;
    using dillen::compatibility::hoi3::content::ProvinceHistoryField;

    dillen::compatibility::hoi3::content::DefinitionRegistry definitions;
    if (!DeclareCountry(definitions, "CHI")
        || !DeclareCountry(definitions, "JAP")
        || !DeclareProvince(definitions, 1, {1, 2, 3})
        || !DeclareProvince(definitions, 2, {4, 5, 6}))
    {
        std::cerr << "WorldBuilder fixture declarations failed\n";
        return 1;
    }
    const auto chinaTag = dillen::compatibility::hoi3::content::CountryTag::Parse("CHI");
    const auto japanTag = dillen::compatibility::hoi3::content::CountryTag::Parse("JAP");
    const dillen::compatibility::hoi3::content::CountryDefinitionId china = chinaTag->StableId();
    const dillen::compatibility::hoi3::content::CountryDefinitionId japan = japanTag->StableId();
    const auto earlyOob = DeclareOrderOfBattle(
        definitions,
        "history/units/CHI_1936.txt"
    );
    const auto warOob = DeclareOrderOfBattle(
        definitions,
        "history/units/CHI_1937.txt"
    );
    if (!earlyOob || !warOob)
    {
        std::cerr << "WorldBuilder OOB fixture failed\n";
        return 2;
    }
    dillen::compatibility::hoi3::content::BookmarkDefinition bookmark;
    bookmark.key = dillen::compatibility::hoi3::content::NormalizeBookmarkKey("ROAD_TO_WAR_NAME");
    bookmark.id = dillen::compatibility::hoi3::content::StableBookmarkDefinitionId(bookmark.key);
    bookmark.name = "ROAD_TO_WAR_NAME";
    bookmark.date = {1936, 1, 1};
    bookmark.origin = Origin("common/bookmarks.txt");
    const auto bookmarkId = bookmark.id;
    dillen::compatibility::hoi3::content::ScenarioDefinition scenario;
    scenario.key = dillen::compatibility::hoi3::content::NormalizeScenarioKey("probe_scenario");
    scenario.id = dillen::compatibility::hoi3::content::StableScenarioDefinitionId(scenario.key);
    scenario.name = "PROBE_SCENARIO_NAME";
    scenario.startDate = {1937, 7, 7};
    scenario.endDate = {1937, 12, 31};
    scenario.origin = Origin("scenarios/probe_scenario.txt");
    const auto scenarioId = scenario.id;
    if (definitions.Launches().Declare(std::move(bookmark))
            != dillen::compatibility::hoi3::content::LaunchDeclareResult::Added
        || definitions.Launches().Declare(std::move(scenario))
            != dillen::compatibility::hoi3::content::LaunchDeclareResult::Added)
    {
        std::cerr << "WorldBuilder launch fixture failed\n";
        return 2;
    }

    dillen::compatibility::hoi3::content::CountryHistorySource chinaHistory;
    chinaHistory.origin = Origin("history/countries/CHI - China.txt");
    chinaHistory.initialOperations = {
        CountryOperation(CountryHistoryField::Capital,
            dillen::compatibility::hoi3::content::ProvinceDefinitionId{1}),
        CountryOperation(CountryHistoryField::Government,
            std::string("chinese_warlord")),
        CountryOperation(CountryHistoryField::Ideology,
            std::string("paternal_autocrat")),
        CountryOperation(CountryHistoryField::Minister,
            std::int64_t{52002}, "head_of_state"),
        CountryOperation(CountryHistoryField::Neutrality, 100.0),
        CountryOperation(CountryHistoryField::SetCountryFlag,
            std::string("initial_china_flag")),
        CountryOperation(CountryHistoryField::NamedAssignment,
            std::string("minimal_training"), "training_laws"),
        CountryOperation(CountryHistoryField::OrderOfBattle, earlyOob)
    };
    dillen::compatibility::hoi3::content::CountryHistoryPatch warPatch;
    warPatch.date = {1937, 7, 7};
    warPatch.origin = chinaHistory.origin;
    warPatch.operations = {
        CountryOperation(CountryHistoryField::Government,
            std::string("nationalist_china")),
        CountryOperation(CountryHistoryField::SetGlobalFlag,
            std::string("china_war_started")),
        CountryOperation(CountryHistoryField::SetManpower, 50.0),
        CountryOperation(CountryHistoryField::CreateAlliance, japan),
        CountryOperation(CountryHistoryField::OrderOfBattle, warOob),
        CountryOperation(CountryHistoryField::LoadOrderOfBattle,
            std::string("history/units/CHI_auxiliary.txt"))
    };
    dillen::compatibility::hoi3::content::CountryHistoryPatch futurePatch;
    futurePatch.date = {1938, 1, 1};
    futurePatch.origin = chinaHistory.origin;
    futurePatch.operations = {
        CountryOperation(CountryHistoryField::Ideology,
            std::string("future_ideology"))
    };
    chinaHistory.patches = {std::move(warPatch), std::move(futurePatch)};
    if (definitions.CountryHistories().Append(
            china,
            std::move(chinaHistory))
        != dillen::compatibility::hoi3::content::CountryHistoryAppendResult::Added)
    {
        std::cerr << "WorldBuilder Country history fixture failed\n";
        return 3;
    }

    dillen::compatibility::hoi3::content::CountryHistorySource sameDayOverride;
    sameDayOverride.origin = Origin(
        "scenarios/probe/CHI - Same Day Override.txt"
    );
    dillen::compatibility::hoi3::content::CountryHistoryPatch overridePatch;
    overridePatch.date = {1937, 7, 7};
    overridePatch.origin = sameDayOverride.origin;
    overridePatch.operations = {
        CountryOperation(CountryHistoryField::Government,
            std::string("coalition_government"))
    };
    sameDayOverride.patches.push_back(std::move(overridePatch));
    if (definitions.CountryHistories().Append(
            china,
            std::move(sameDayOverride))
        != dillen::compatibility::hoi3::content::CountryHistoryAppendResult::Merged)
    {
        std::cerr << "WorldBuilder same-day fixture failed\n";
        return 4;
    }

    dillen::compatibility::hoi3::content::ProvinceHistorySource provinceHistory;
    provinceHistory.origin = Origin("history/provinces/china/1 - Test.txt");
    provinceHistory.initialOperations = {
        ProvinceOperation(ProvinceHistoryField::Owner, china),
        ProvinceOperation(ProvinceHistoryField::Controller, china),
        ProvinceOperation(ProvinceHistoryField::AddCore, china),
        ProvinceOperation(ProvinceHistoryField::Infrastructure,
            std::int64_t{5})
    };
    dillen::compatibility::hoi3::content::ProvinceHistoryPatch occupationPatch;
    occupationPatch.date = {1937, 7, 7};
    occupationPatch.origin = provinceHistory.origin;
    occupationPatch.operations = {
        ProvinceOperation(ProvinceHistoryField::Controller, japan),
        ProvinceOperation(ProvinceHistoryField::RemoveCore, china),
        ProvinceOperation(ProvinceHistoryField::Industry,
            std::int64_t{4})
    };
    dillen::compatibility::hoi3::content::ProvinceHistoryPatch ownerPatch;
    ownerPatch.date = {1938, 1, 1};
    ownerPatch.origin = provinceHistory.origin;
    ownerPatch.operations = {
        ProvinceOperation(ProvinceHistoryField::Owner, japan)
    };
    provinceHistory.patches = {
        std::move(occupationPatch),
        std::move(ownerPatch)
    };
    if (definitions.ProvinceHistories().Append(
            dillen::compatibility::hoi3::content::ProvinceDefinitionId{1},
            std::move(provinceHistory))
        != dillen::compatibility::hoi3::content::ProvinceHistoryAppendResult::Added)
    {
        std::cerr << "WorldBuilder Province history fixture failed\n";
        return 5;
    }

    compatibility::hoi3::worldbuilder::WorldBuilder builder;
    compatibility::hoi3::worldbuilder::Hoi3WorldState world;
    compatibility::hoi3::worldbuilder::WorldBuildReport report;
    if (builder.Build(definitions, {1936, 1, 1}, world, report)
        || !HasIssue(report, "worldbuilder.registry_not_frozen"))
    {
        std::cerr << "WorldBuilder accepted an unfrozen registry\n";
        return 6;
    }

    definitions.Freeze();
    if (!builder.BuildBookmark(definitions, bookmarkId, world, report))
    {
        std::cerr << "WorldBuilder initial snapshot failed\n";
        return 7;
    }
    const auto* earlyChina = world.FindCountry("CHI");
    const auto* earlyJapan = world.FindCountry("JAP");
    const auto* earlyProvince = world.FindProvince(1);
    const auto earlyInfrastructure = earlyProvince == nullptr
        ? std::nullopt
        : earlyProvince->Numeric(ProvinceHistoryField::Infrastructure);
    const auto* earlyUnit = earlyChina == nullptr
            || earlyChina->unitRoots.size() != 1
        ? nullptr
        : world.FindUnit(earlyChina->unitRoots.front());
    if (world.Countries().size() != 2
        || world.Provinces().size() != 2
        || world.Units().size() != 1
        || earlyChina == nullptr
        || earlyJapan == nullptr
        || earlyProvince == nullptr
        || earlyUnit == nullptr
        || earlyChina->government != "chinese_warlord"
        || earlyChina->ideology != "paternal_autocrat"
        || earlyChina->ordersOfBattle.size() != 1
        || earlyChina->ordersOfBattle.front().definition != earlyOob
        || earlyUnit->source != earlyOob
        || earlyUnit->country != china
        || earlyUnit->location != dillen::compatibility::hoi3::content::ProvinceDefinitionId{1}
        || earlyUnit->parent.has_value()
        || earlyProvince->locatedUnits.size() != 1
        || earlyProvince->locatedUnits.front() != earlyUnit->id
        || earlyChina->ownedProvinces
            != std::vector<dillen::compatibility::hoi3::content::ProvinceDefinitionId>{{1}}
        || earlyChina->controlledProvinces
            != std::vector<dillen::compatibility::hoi3::content::ProvinceDefinitionId>{{1}}
        || earlyChina->coreProvinces
            != std::vector<dillen::compatibility::hoi3::content::ProvinceDefinitionId>{{1}}
        || !earlyJapan->ownedProvinces.empty()
        || !earlyJapan->controlledProvinces.empty()
        || !earlyJapan->coreProvinces.empty()
        || earlyProvince->owner != china
        || earlyProvince->controller != china
        || earlyProvince->cores.count(china) != 1
        || !earlyInfrastructure
        || !Approximately(*earlyInfrastructure, 5.0)
        || !world.Relations().empty()
        || !world.GlobalFlags().empty()
        || world.Bookmark() != bookmarkId
        || world.Scenario().has_value())
    {
        std::cerr << "WorldBuilder initial snapshot mismatch\n";
        return 8;
    }

    if (!builder.BuildScenario(definitions, scenarioId, world, report))
    {
        std::cerr << "WorldBuilder dated snapshot failed\n";
        return 9;
    }
    const auto* warChina = world.FindCountry("CHI");
    const auto* warJapan = world.FindCountry("JAP");
    const auto* occupiedProvince = world.FindProvince(1);
    const auto industry = occupiedProvince == nullptr
        ? std::nullopt
        : occupiedProvince->Numeric(ProvinceHistoryField::Industry);
    const auto* warUnit = warChina == nullptr
            || warChina->unitRoots.size() != 1
        ? nullptr
        : world.FindUnit(warChina->unitRoots.front());
    const dillen::compatibility::hoi3::content::DiplomacyHistoryKey allianceKey =
        dillen::compatibility::hoi3::content::CanonicalDiplomacyHistoryKey(
            dillen::compatibility::hoi3::content::DiplomaticRelationKind::Alliance,
            china,
            japan
        );
    if (warChina == nullptr
        || warJapan == nullptr
        || occupiedProvince == nullptr
        || world.Units().size() != 1
        || warUnit == nullptr
        || warChina->government != "coalition_government"
        || warChina->ideology != "paternal_autocrat"
        || warChina->manpower != 50.0
        || warChina->alliances.count(japan) != 1
        || warJapan->alliances.count(china) != 1
        || world.Relations().size() != 1
        || world.Relations().front().kind != allianceKey.kind
        || world.Relations().front().first != allianceKey.first
        || world.Relations().front().second != allianceKey.second
        || warChina->ordersOfBattle.size() != 2
        || warChina->ordersOfBattle[0].definition != warOob
        || !warChina->ordersOfBattle[1].additive
        || warChina->ordersOfBattle[1].unresolvedPath
            != "history/units/CHI_auxiliary.txt"
        || warUnit->source != warOob
        || warUnit->country != china
        || warUnit->location != dillen::compatibility::hoi3::content::ProvinceDefinitionId{1}
        || occupiedProvince->locatedUnits.size() != 1
        || occupiedProvince->locatedUnits.front() != warUnit->id
        || warChina->ownedProvinces
            != std::vector<dillen::compatibility::hoi3::content::ProvinceDefinitionId>{{1}}
        || !warChina->controlledProvinces.empty()
        || !warChina->coreProvinces.empty()
        || !warJapan->ownedProvinces.empty()
        || warJapan->controlledProvinces
            != std::vector<dillen::compatibility::hoi3::content::ProvinceDefinitionId>{{1}}
        || !warJapan->coreProvinces.empty()
        || world.GlobalFlags().count("china_war_started") != 1
        || occupiedProvince->owner != china
        || occupiedProvince->controller != japan
        || occupiedProvince->cores.count(china) != 0
        || !industry
        || !Approximately(*industry, 4.0)
        || report.WarningCount() != 1
        || !HasIssue(report, "worldbuilder.oob_reference_unresolved")
        || world.Scenario() != scenarioId
        || world.Bookmark().has_value())
    {
        std::cerr << "WorldBuilder dated snapshot mismatch\n";
        return 10;
    }

    if (builder.Build(definitions, {1937, 2, 29}, world, report)
        || !HasIssue(report, "worldbuilder.date_invalid")
        || world.Date() != dillen::compatibility::hoi3::content::DefinitionDate{1937, 7, 7})
    {
        std::cerr << "WorldBuilder transactional failure mismatch\n";
        return 11;
    }

    std::cout
        << "WorldBuilder: passed ("
        << world.Countries().size() << " countries, "
        << world.Provinces().size() << " provinces, dated replay)\n";
    return 0;
}
