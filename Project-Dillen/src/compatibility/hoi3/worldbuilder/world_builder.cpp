#include "world_builder.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace dillen::compatibility::hoi3::worldbuilder {

namespace {

bool DateIsAtOrBefore(
    dillen::compatibility::hoi3::content::DefinitionDate first,
    dillen::compatibility::hoi3::content::DefinitionDate second
)
{
    return !(second < first);
}

std::optional<double> ReadNumber(
    const dillen::compatibility::hoi3::content::CountryHistoryValue& value
)
{
    if (const auto* decimal = std::get_if<double>(&value))
    {
        return *decimal;
    }
    if (const auto* integer = std::get_if<std::int64_t>(&value))
    {
        return static_cast<double>(*integer);
    }
    return std::nullopt;
}

std::optional<double> ReadNumber(
    const dillen::compatibility::hoi3::content::ProvinceHistoryValue& value
)
{
    if (const auto* decimal = std::get_if<double>(&value))
    {
        return *decimal;
    }
    if (const auto* integer = std::get_if<std::int64_t>(&value))
    {
        return static_cast<double>(*integer);
    }
    return std::nullopt;
}

bool IsProvinceNumericField(dillen::compatibility::hoi3::content::ProvinceHistoryField field)
{
    using dillen::compatibility::hoi3::content::ProvinceHistoryField;
    switch (field)
    {
    case ProvinceHistoryField::Infrastructure:
    case ProvinceHistoryField::Industry:
    case ProvinceHistoryField::VictoryPoints:
    case ProvinceHistoryField::NavalBase:
    case ProvinceHistoryField::AirBase:
    case ProvinceHistoryField::AntiAir:
    case ProvinceHistoryField::LandFort:
    case ProvinceHistoryField::CoastalFort:
    case ProvinceHistoryField::RadarStation:
    case ProvinceHistoryField::RocketTest:
    case ProvinceHistoryField::Manpower:
    case ProvinceHistoryField::Leadership:
    case ProvinceHistoryField::Energy:
    case ProvinceHistoryField::Metal:
    case ProvinceHistoryField::RareMaterials:
    case ProvinceHistoryField::CrudeOil:
    case ProvinceHistoryField::Fuel:
    case ProvinceHistoryField::Supplies:
        return true;
    default:
        return false;
    }
}

std::string CountryIdText(dillen::compatibility::hoi3::content::CountryDefinitionId id)
{
    return std::to_string(id.value);
}

std::string ProvinceIdText(dillen::compatibility::hoi3::content::ProvinceDefinitionId id)
{
    return std::to_string(id.value);
}

bool ContainsProvince(
    const std::vector<dillen::compatibility::hoi3::content::ProvinceDefinitionId>& provinces,
    dillen::compatibility::hoi3::content::ProvinceDefinitionId province
)
{
    return std::binary_search(provinces.begin(), provinces.end(), province);
}

}

void WorldBuildReport::Clear()
{
    issues_.clear();
}

void WorldBuildReport::Warning(
    std::string code,
    std::string message,
    dillen::compatibility::hoi3::content::DefinitionOrigin origin
)
{
    issues_.push_back({
        WorldBuildIssueSeverity::Warning,
        std::move(code),
        std::move(message),
        std::move(origin)
    });
}

void WorldBuildReport::Error(
    std::string code,
    std::string message,
    dillen::compatibility::hoi3::content::DefinitionOrigin origin
)
{
    issues_.push_back({
        WorldBuildIssueSeverity::Error,
        std::move(code),
        std::move(message),
        std::move(origin)
    });
}

bool WorldBuildReport::HasErrors() const noexcept
{
    return ErrorCount() != 0;
}

std::size_t WorldBuildReport::WarningCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        issues_.begin(),
        issues_.end(),
        [](const WorldBuildIssue& issue)
        {
            return issue.severity == WorldBuildIssueSeverity::Warning;
        }
    ));
}

std::size_t WorldBuildReport::ErrorCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        issues_.begin(),
        issues_.end(),
        [](const WorldBuildIssue& issue)
        {
            return issue.severity == WorldBuildIssueSeverity::Error;
        }
    ));
}

const std::vector<WorldBuildIssue>& WorldBuildReport::All() const noexcept
{
    return issues_;
}

bool IsValidWorldDate(dillen::compatibility::hoi3::content::DefinitionDate date) noexcept
{
    if (date.year < 1 || date.month < 1 || date.month > 12 || date.day < 1)
    {
        return false;
    }
    static constexpr int daysPerMonth[] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };
    int days = daysPerMonth[date.month - 1];
    const bool leapYear = date.year % 4 == 0
        && (date.year % 100 != 0 || date.year % 400 == 0);
    if (date.month == 2 && leapYear)
    {
        ++days;
    }
    return date.day <= days;
}

bool WorldBuilder::Build(
    const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
    dillen::compatibility::hoi3::content::DefinitionDate date,
    Hoi3WorldState& output,
    WorldBuildReport& report
) const
{
    return BuildInternal(definitions, nullptr, date, output, report);
}

bool WorldBuilder::Build(
    const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
    const kernel::FrozenRuntimeCatalog& runtimeCatalog,
    dillen::compatibility::hoi3::content::DefinitionDate date,
    Hoi3WorldState& output,
    WorldBuildReport& report
) const
{
    return BuildInternal(
        definitions,
        &runtimeCatalog,
        date,
        output,
        report
    );
}

bool WorldBuilder::BuildInternal(
    const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
    const kernel::FrozenRuntimeCatalog* runtimeCatalog,
    dillen::compatibility::hoi3::content::DefinitionDate date,
    Hoi3WorldState& output,
    WorldBuildReport& report
) const
{
    report.Clear();
    if (!definitions.IsFrozen())
    {
        report.Error(
            "worldbuilder.registry_not_frozen",
            "Definition Registry must be frozen before world construction"
        );
        return false;
    }
    if (runtimeCatalog != nullptr
        && !runtimeCatalog->IsFrozen())
    {
        report.Error(
            "worldbuilder.runtime_catalog_not_frozen",
            "Frozen Runtime Catalog is required before mechanism instantiation"
        );
        return false;
    }
    if (!IsValidWorldDate(date))
    {
        report.Error(
            "worldbuilder.date_invalid",
            "world construction date is not a valid Gregorian date"
        );
        return false;
    }

    Hoi3WorldState candidate;
    candidate.date_ = date;
    candidate.countries_.reserve(definitions.Countries().Size());
    for (const dillen::compatibility::hoi3::content::CountryTagDefinition& definition
        : definitions.Countries().All())
    {
        CountryState country;
        country.id = definition.id;
        country.tag = definition.tag;
        candidate.countries_.push_back(std::move(country));
    }
    candidate.provinces_.reserve(definitions.Provinces().Size());
    for (const dillen::compatibility::hoi3::content::ProvinceDefinition& definition
        : definitions.Provinces().All())
    {
        ProvinceState province;
        province.id = definition.id;
        candidate.provinces_.push_back(std::move(province));
    }

    for (const dillen::compatibility::hoi3::content::CountryHistoryTimeline& timeline
        : definitions.CountryHistories().All())
    {
        CountryState* country = candidate.FindCountryMutable(
            timeline.country
        );
        if (country == nullptr)
        {
            report.Error(
                "worldbuilder.country_history_target_missing",
                "Country history targets undeclared Country ID "
                    + CountryIdText(timeline.country)
            );
            continue;
        }
        for (const dillen::compatibility::hoi3::content::CountryHistoryOperation& operation
            : timeline.initialOperations)
        {
            ApplyCountryOperation(
                definitions,
                operation,
                *country,
                candidate,
                report
            );
        }
        for (const dillen::compatibility::hoi3::content::CountryHistoryPatch& patch : timeline.patches)
        {
            if (!DateIsAtOrBefore(patch.date, date))
            {
                break;
            }
            for (const dillen::compatibility::hoi3::content::CountryHistoryOperation& operation
                : patch.operations)
            {
                ApplyCountryOperation(
                    definitions,
                    operation,
                    *country,
                    candidate,
                    report
                );
            }
        }
    }

    for (const dillen::compatibility::hoi3::content::ProvinceHistoryTimeline& timeline
        : definitions.ProvinceHistories().All())
    {
        ProvinceState* province = candidate.FindProvinceMutable(
            timeline.province
        );
        if (province == nullptr)
        {
            report.Error(
                "worldbuilder.province_history_target_missing",
                "Province history targets undeclared Province ID "
                    + ProvinceIdText(timeline.province)
            );
            continue;
        }
        for (const dillen::compatibility::hoi3::content::ProvinceHistoryOperation& operation
            : timeline.initialOperations)
        {
            ApplyProvinceOperation(
                definitions,
                operation,
                *province,
                report
            );
        }
        for (const dillen::compatibility::hoi3::content::ProvinceHistoryPatch& patch : timeline.patches)
        {
            if (!DateIsAtOrBefore(patch.date, date))
            {
                break;
            }
            for (const dillen::compatibility::hoi3::content::ProvinceHistoryOperation& operation
                : patch.operations)
            {
                ApplyProvinceOperation(
                    definitions,
                    operation,
                    *province,
                    report
                );
            }
        }
    }

    if (!BuildCountryRelationGraph(definitions, date, candidate, report)
        || !ValidateCountryRelationGraph(candidate, report)
        || !BuildRuntimeWarGraph(definitions, date, candidate, report)
        || !ValidateRuntimeWarGraph(candidate, report)
        || !BuildTerritorialIndexes(candidate, report)
        || !ValidateTerritorialIndexes(candidate, report)
        || !InstantiateRuntimeUnits(definitions, candidate, report)
        || (runtimeCatalog != nullptr
            && !BuildKernelWorld(
                *runtimeCatalog,
                candidate,
                report)))
    {
        return false;
    }

    if (report.HasErrors())
    {
        return false;
    }
    output = std::move(candidate);
    return true;
}

bool WorldBuilder::BuildKernelWorld(
    const kernel::FrozenRuntimeCatalog& runtimeCatalog,
    Hoi3WorldState& world,
    WorldBuildReport& report
)
{
    dillen::world::InitialWorldBuildReport kernelReport;
    dillen::world::AuthoritativeWorld candidate;
    if (!dillen::world::InitialWorldBuilder{}.Build(
            runtimeCatalog,
            candidate,
            kernelReport))
    {
        const std::string message = kernelReport.issues.empty()
            ? "Kernel World construction failed"
            : kernelReport.issues.front().message;
        report.Error(
            "worldbuilder.kernel_world_failed",
            message
        );
        return false;
    }
    world.world_ = std::move(candidate);
    return true;
}

bool WorldBuilder::BuildCountryRelationGraph(
    const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
    dillen::compatibility::hoi3::content::DefinitionDate date,
    Hoi3WorldState& world,
    WorldBuildReport& report
)
{
    std::vector<CountryRelationState> historyAlliances;
    for (const CountryState& country : world.countries_)
    {
        for (dillen::compatibility::hoi3::content::CountryDefinitionId ally : country.alliances)
        {
            const CountryState* other = world.FindCountry(ally);
            if (other == nullptr || other->alliances.count(country.id) == 0)
            {
                report.Error(
                    "worldbuilder.relation_history_alliance_asymmetric",
                    "Country history alliance is not symmetric"
                );
                return false;
            }
            if (country.id < ally)
            {
                historyAlliances.push_back({
                    dillen::compatibility::hoi3::content::DiplomaticRelationKind::Alliance,
                    country.id,
                    ally
                });
            }
        }
    }

    world.relations_.clear();
    for (CountryState& country : world.countries_)
    {
        country.alliances.clear();
        country.guarantees.clear();
        country.guaranteedBy.clear();
        country.subjects.clear();
        country.overlord.reset();
    }

    bool success = true;
    for (const CountryRelationState& relation : historyAlliances)
    {
        if (!AddCountryRelation(
                relation.kind,
                relation.first,
                relation.second,
                world,
                report))
        {
            success = false;
        }
    }
    for (const dillen::compatibility::hoi3::content::DiplomacyHistoryTimeline& timeline
        : definitions.DiplomacyHistories().All())
    {
        const bool active = std::any_of(
            timeline.periods.begin(),
            timeline.periods.end(),
            [date](const dillen::compatibility::hoi3::content::DiplomaticRelationPeriod& period)
            {
                return DateIsAtOrBefore(period.startDate, date)
                    && date < period.endDate;
            }
        );
        if (active
            && !AddCountryRelation(
                timeline.key.kind,
                timeline.key.first,
                timeline.key.second,
                world,
                report))
        {
            success = false;
        }
    }
    std::sort(
        world.relations_.begin(),
        world.relations_.end(),
        [](const CountryRelationState& first,
           const CountryRelationState& second)
        {
            return dillen::compatibility::hoi3::content::DiplomacyHistoryKey{
                    first.kind, first.first, first.second}
                < dillen::compatibility::hoi3::content::DiplomacyHistoryKey{
                    second.kind, second.first, second.second};
        }
    );
    return success;
}

bool WorldBuilder::AddCountryRelation(
    dillen::compatibility::hoi3::content::DiplomaticRelationKind kind,
    dillen::compatibility::hoi3::content::CountryDefinitionId first,
    dillen::compatibility::hoi3::content::CountryDefinitionId second,
    Hoi3WorldState& world,
    WorldBuildReport& report
)
{
    const dillen::compatibility::hoi3::content::DiplomacyHistoryKey key =
        dillen::compatibility::hoi3::content::CanonicalDiplomacyHistoryKey(kind, first, second);
    if (!key.first || !key.second || key.first == key.second)
    {
        report.Error(
            "worldbuilder.relation_key_invalid",
            "Country relation key is invalid"
        );
        return false;
    }
    CountryState* firstCountry = world.FindCountryMutable(key.first);
    CountryState* secondCountry = world.FindCountryMutable(key.second);
    if (firstCountry == nullptr || secondCountry == nullptr)
    {
        report.Error(
            "worldbuilder.relation_country_missing",
            "Country relation references a missing Country"
        );
        return false;
    }
    const auto duplicate = std::find_if(
        world.relations_.begin(),
        world.relations_.end(),
        [&key](const CountryRelationState& relation)
        {
            return relation.kind == key.kind
                && relation.first == key.first
                && relation.second == key.second;
        }
    );
    if (duplicate != world.relations_.end())
    {
        return true;
    }

    if (key.kind == dillen::compatibility::hoi3::content::DiplomaticRelationKind::Alliance)
    {
        firstCountry->alliances.insert(key.second);
        secondCountry->alliances.insert(key.first);
    }
    else if (key.kind == dillen::compatibility::hoi3::content::DiplomaticRelationKind::Guarantee)
    {
        firstCountry->guarantees.insert(key.second);
        secondCountry->guaranteedBy.insert(key.first);
    }
    else
    {
        if (secondCountry->overlord
            && secondCountry->overlord != key.first)
        {
            report.Error(
                "worldbuilder.relation_multiple_overlords",
                "a subject Country cannot have multiple active overlords"
            );
            return false;
        }
        firstCountry->subjects.insert(key.second);
        secondCountry->overlord = key.first;
    }
    world.relations_.push_back({key.kind, key.first, key.second});
    return true;
}

bool WorldBuilder::ValidateCountryRelationGraph(
    const Hoi3WorldState& world,
    WorldBuildReport& report
)
{
    const auto hasRelation = [&world](
        dillen::compatibility::hoi3::content::DiplomaticRelationKind kind,
        dillen::compatibility::hoi3::content::CountryDefinitionId first,
        dillen::compatibility::hoi3::content::CountryDefinitionId second)
    {
        const dillen::compatibility::hoi3::content::DiplomacyHistoryKey key =
            dillen::compatibility::hoi3::content::CanonicalDiplomacyHistoryKey(kind, first, second);
        return std::any_of(
            world.relations_.begin(),
            world.relations_.end(),
            [&key](const CountryRelationState& relation)
            {
                return relation.kind == key.kind
                    && relation.first == key.first
                    && relation.second == key.second;
            }
        );
    };

    bool valid = true;
    for (const CountryRelationState& relation : world.relations_)
    {
        const CountryState* first = world.FindCountry(relation.first);
        const CountryState* second = world.FindCountry(relation.second);
        if (first == nullptr || second == nullptr)
        {
            report.Error(
                "worldbuilder.relation_graph_country_missing",
                "Country relation graph contains a missing Country"
            );
            valid = false;
            continue;
        }
        bool edgeValid = false;
        if (relation.kind == dillen::compatibility::hoi3::content::DiplomaticRelationKind::Alliance)
        {
            edgeValid = first->alliances.count(second->id) != 0
                && second->alliances.count(first->id) != 0;
        }
        else if (relation.kind == dillen::compatibility::hoi3::content::DiplomaticRelationKind::Guarantee)
        {
            edgeValid = first->guarantees.count(second->id) != 0
                && second->guaranteedBy.count(first->id) != 0;
        }
        else
        {
            edgeValid = first->subjects.count(second->id) != 0
                && second->overlord == first->id;
        }
        if (!edgeValid)
        {
            report.Error(
                "worldbuilder.relation_graph_reverse_missing",
                "Country relation graph and adjacency indexes disagree"
            );
            valid = false;
        }
    }

    for (const CountryState& country : world.countries_)
    {
        for (dillen::compatibility::hoi3::content::CountryDefinitionId ally : country.alliances)
        {
            const CountryState* other = world.FindCountry(ally);
            if (other == nullptr
                || other->alliances.count(country.id) == 0
                || !hasRelation(
                    dillen::compatibility::hoi3::content::DiplomaticRelationKind::Alliance,
                    country.id,
                    ally))
            {
                report.Error(
                    "worldbuilder.relation_alliance_asymmetric",
                    "Alliance adjacency is not symmetric"
                );
                valid = false;
            }
        }
        for (dillen::compatibility::hoi3::content::CountryDefinitionId target : country.guarantees)
        {
            const CountryState* other = world.FindCountry(target);
            if (other == nullptr
                || other->guaranteedBy.count(country.id) == 0
                || !hasRelation(
                    dillen::compatibility::hoi3::content::DiplomaticRelationKind::Guarantee,
                    country.id,
                    target))
            {
                report.Error(
                    "worldbuilder.relation_guarantee_reverse_missing",
                    "Guarantee adjacency is inconsistent"
                );
                valid = false;
            }
        }
        for (dillen::compatibility::hoi3::content::CountryDefinitionId source : country.guaranteedBy)
        {
            if (!hasRelation(
                    dillen::compatibility::hoi3::content::DiplomaticRelationKind::Guarantee,
                    source,
                    country.id))
            {
                report.Error(
                    "worldbuilder.relation_guaranteed_by_forward_missing",
                    "Guaranteed-by adjacency is inconsistent"
                );
                valid = false;
            }
        }
        for (dillen::compatibility::hoi3::content::CountryDefinitionId subject : country.subjects)
        {
            const CountryState* other = world.FindCountry(subject);
            if (other == nullptr
                || other->overlord != country.id
                || !hasRelation(
                    dillen::compatibility::hoi3::content::DiplomaticRelationKind::Vassal,
                    country.id,
                    subject))
            {
                report.Error(
                    "worldbuilder.relation_subject_reverse_missing",
                    "Subject adjacency is inconsistent"
                );
                valid = false;
            }
        }
        if (country.overlord
            && !hasRelation(
                dillen::compatibility::hoi3::content::DiplomaticRelationKind::Vassal,
                *country.overlord,
                country.id))
        {
            report.Error(
                "worldbuilder.relation_overlord_forward_missing",
                "Overlord adjacency is inconsistent"
            );
            valid = false;
        }
    }
    return valid;
}

bool WorldBuilder::BuildRuntimeWarGraph(
    const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
    dillen::compatibility::hoi3::content::DefinitionDate date,
    Hoi3WorldState& world,
    WorldBuildReport& report
)
{
    world.wars_.clear();
    for (CountryState& country : world.countries_)
    {
        country.offensiveWars.clear();
        country.defensiveWars.clear();
    }

    bool success = true;
    for (const dillen::compatibility::hoi3::content::WarHistoryTimeline& timeline
        : definitions.WarHistories().All())
    {
        RuntimeWarState war;
        war.id = timeline.id;
        war.name = timeline.name;
        war.limitedWar = timeline.limitedWar;

        for (const dillen::compatibility::hoi3::content::WarHistoryPatch& patch : timeline.patches)
        {
            if (!DateIsAtOrBefore(patch.date, date))
            {
                break;
            }
            for (const dillen::compatibility::hoi3::content::WarParticipantOperation& operation
                : patch.participantOperations)
            {
                using dillen::compatibility::hoi3::content::WarParticipantOperationKind;
                if (operation.kind
                    == WarParticipantOperationKind::AddAttacker)
                {
                    if (war.defenders.count(operation.country) != 0)
                    {
                        report.Error(
                            "worldbuilder.war_participant_side_conflict",
                            "a Country cannot join both sides of a War",
                            operation.origin
                        );
                        success = false;
                    }
                    else
                    {
                        war.attackers.insert(operation.country);
                    }
                }
                else if (operation.kind
                    == WarParticipantOperationKind::RemoveAttacker)
                {
                    war.attackers.erase(operation.country);
                }
                else if (operation.kind
                    == WarParticipantOperationKind::AddDefender)
                {
                    if (war.attackers.count(operation.country) != 0)
                    {
                        report.Error(
                            "worldbuilder.war_participant_side_conflict",
                            "a Country cannot join both sides of a War",
                            operation.origin
                        );
                        success = false;
                    }
                    else
                    {
                        war.defenders.insert(operation.country);
                    }
                }
                else
                {
                    war.defenders.erase(operation.country);
                }
            }
            war.warGoals.insert(
                war.warGoals.end(),
                patch.warGoals.begin(),
                patch.warGoals.end()
            );
        }

        if (war.attackers.empty() || war.defenders.empty())
        {
            continue;
        }
        for (dillen::compatibility::hoi3::content::CountryDefinitionId participant : war.attackers)
        {
            CountryState* country = world.FindCountryMutable(participant);
            if (country == nullptr)
            {
                report.Error(
                    "worldbuilder.war_attacker_missing",
                    "War attacker references a missing Country",
                    timeline.origin
                );
                success = false;
            }
            else
            {
                country->offensiveWars.insert(war.id);
            }
        }
        for (dillen::compatibility::hoi3::content::CountryDefinitionId participant : war.defenders)
        {
            CountryState* country = world.FindCountryMutable(participant);
            if (country == nullptr)
            {
                report.Error(
                    "worldbuilder.war_defender_missing",
                    "War defender references a missing Country",
                    timeline.origin
                );
                success = false;
            }
            else
            {
                country->defensiveWars.insert(war.id);
            }
        }
        world.wars_.push_back(std::move(war));
    }
    std::sort(
        world.wars_.begin(),
        world.wars_.end(),
        [](const RuntimeWarState& first, const RuntimeWarState& second)
        {
            return first.id < second.id;
        }
    );
    return success;
}

bool WorldBuilder::ValidateRuntimeWarGraph(
    const Hoi3WorldState& world,
    WorldBuildReport& report
)
{
    bool valid = true;
    for (const RuntimeWarState& war : world.wars_)
    {
        if (war.attackers.empty() || war.defenders.empty())
        {
            report.Error(
                "worldbuilder.war_side_empty",
                "active War must contain attackers and defenders"
            );
            valid = false;
        }
        for (dillen::compatibility::hoi3::content::CountryDefinitionId attackerId : war.attackers)
        {
            const CountryState* attacker = world.FindCountry(attackerId);
            if (attacker == nullptr
                || attacker->offensiveWars.count(war.id) == 0)
            {
                report.Error(
                    "worldbuilder.war_attacker_reverse_missing",
                    "War attacker reverse index is inconsistent"
                );
                valid = false;
                continue;
            }
            if (war.defenders.count(attackerId) != 0)
            {
                report.Error(
                    "worldbuilder.war_side_overlap",
                    "active War sides overlap"
                );
                valid = false;
            }
            for (dillen::compatibility::hoi3::content::CountryDefinitionId defenderId : war.defenders)
            {
                if (attacker->alliances.count(defenderId) != 0)
                {
                    report.Error(
                        "worldbuilder.war_opposing_alliance",
                        "opposing War participants retain an active Alliance"
                    );
                    valid = false;
                }
                if (attacker->subjects.count(defenderId) != 0
                    || attacker->overlord == defenderId)
                {
                    report.Error(
                        "worldbuilder.war_opposing_vassal",
                        "overlord and subject cannot occupy opposing War sides"
                    );
                    valid = false;
                }
            }
        }
        for (dillen::compatibility::hoi3::content::CountryDefinitionId defenderId : war.defenders)
        {
            const CountryState* defender = world.FindCountry(defenderId);
            if (defender == nullptr
                || defender->defensiveWars.count(war.id) == 0)
            {
                report.Error(
                    "worldbuilder.war_defender_reverse_missing",
                    "War defender reverse index is inconsistent"
                );
                valid = false;
            }
        }
        for (const dillen::compatibility::hoi3::content::WarGoalDefinition& goal : war.warGoals)
        {
            if (world.FindCountry(goal.actor) == nullptr
                || world.FindCountry(goal.receiver) == nullptr)
            {
                report.Error(
                    "worldbuilder.war_goal_country_missing",
                    "War goal references a missing Country",
                    goal.origin
                );
                valid = false;
            }
        }
    }

    for (const CountryState& country : world.countries_)
    {
        for (dillen::compatibility::hoi3::content::WarHistoryDefinitionId warId : country.offensiveWars)
        {
            const RuntimeWarState* war = world.FindWar(warId);
            if (war == nullptr || war->attackers.count(country.id) == 0)
            {
                report.Error(
                    "worldbuilder.country_offensive_war_forward_missing",
                    "Country offensive War index is inconsistent"
                );
                valid = false;
            }
        }
        for (dillen::compatibility::hoi3::content::WarHistoryDefinitionId warId : country.defensiveWars)
        {
            const RuntimeWarState* war = world.FindWar(warId);
            if (war == nullptr || war->defenders.count(country.id) == 0)
            {
                report.Error(
                    "worldbuilder.country_defensive_war_forward_missing",
                    "Country defensive War index is inconsistent"
                );
                valid = false;
            }
            if (country.offensiveWars.count(warId) != 0)
            {
                report.Error(
                    "worldbuilder.country_war_side_overlap",
                    "Country appears on both sides of one War"
                );
                valid = false;
            }
        }
    }
    return valid;
}

bool WorldBuilder::BuildTerritorialIndexes(
    Hoi3WorldState& world,
    WorldBuildReport& report
)
{
    for (CountryState& country : world.countries_)
    {
        country.ownedProvinces.clear();
        country.controlledProvinces.clear();
        country.coreProvinces.clear();
    }

    bool success = true;
    for (const ProvinceState& province : world.provinces_)
    {
        if (province.owner)
        {
            CountryState* owner = world.FindCountryMutable(*province.owner);
            if (owner == nullptr)
            {
                report.Error(
                    "worldbuilder.territory_owner_country_missing",
                    "Province " + ProvinceIdText(province.id)
                        + " references a missing owner Country"
                );
                success = false;
            }
            else
            {
                owner->ownedProvinces.push_back(province.id);
            }
        }
        if (province.controller)
        {
            CountryState* controller = world.FindCountryMutable(
                *province.controller
            );
            if (controller == nullptr)
            {
                report.Error(
                    "worldbuilder.territory_controller_country_missing",
                    "Province " + ProvinceIdText(province.id)
                        + " references a missing controller Country"
                );
                success = false;
            }
            else
            {
                controller->controlledProvinces.push_back(province.id);
            }
        }
        for (dillen::compatibility::hoi3::content::CountryDefinitionId core : province.cores)
        {
            CountryState* country = world.FindCountryMutable(core);
            if (country == nullptr)
            {
                report.Error(
                    "worldbuilder.territory_core_country_missing",
                    "Province " + ProvinceIdText(province.id)
                        + " references a missing core Country"
                );
                success = false;
            }
            else
            {
                country->coreProvinces.push_back(province.id);
            }
        }
    }
    return success;
}

bool WorldBuilder::ValidateTerritorialIndexes(
    const Hoi3WorldState& world,
    WorldBuildReport& report
)
{
    bool valid = true;
    for (const ProvinceState& province : world.provinces_)
    {
        if (province.owner)
        {
            const CountryState* country = world.FindCountry(*province.owner);
            if (country == nullptr
                || !ContainsProvince(country->ownedProvinces, province.id))
            {
                report.Error(
                    "worldbuilder.territory_owner_reverse_missing",
                    "Province owner reverse index is inconsistent for Province "
                        + ProvinceIdText(province.id)
                );
                valid = false;
            }
        }
        if (province.controller)
        {
            const CountryState* country = world.FindCountry(
                *province.controller
            );
            if (country == nullptr
                || !ContainsProvince(
                    country->controlledProvinces,
                    province.id))
            {
                report.Error(
                    "worldbuilder.territory_controller_reverse_missing",
                    "Province controller reverse index is inconsistent for Province "
                        + ProvinceIdText(province.id)
                );
                valid = false;
            }
        }
        for (dillen::compatibility::hoi3::content::CountryDefinitionId core : province.cores)
        {
            const CountryState* country = world.FindCountry(core);
            if (country == nullptr
                || !ContainsProvince(country->coreProvinces, province.id))
            {
                report.Error(
                    "worldbuilder.territory_core_reverse_missing",
                    "Province core reverse index is inconsistent for Province "
                        + ProvinceIdText(province.id)
                );
                valid = false;
            }
        }
    }

    for (const CountryState& country : world.countries_)
    {
        for (dillen::compatibility::hoi3::content::ProvinceDefinitionId provinceId
            : country.ownedProvinces)
        {
            const ProvinceState* province = world.FindProvince(provinceId);
            if (province == nullptr || province->owner != country.id)
            {
                report.Error(
                    "worldbuilder.territory_owner_forward_missing",
                    "Country owner index is inconsistent for Province "
                        + ProvinceIdText(provinceId)
                );
                valid = false;
            }
        }
        for (dillen::compatibility::hoi3::content::ProvinceDefinitionId provinceId
            : country.controlledProvinces)
        {
            const ProvinceState* province = world.FindProvince(provinceId);
            if (province == nullptr || province->controller != country.id)
            {
                report.Error(
                    "worldbuilder.territory_controller_forward_missing",
                    "Country controller index is inconsistent for Province "
                        + ProvinceIdText(provinceId)
                );
                valid = false;
            }
        }
        for (dillen::compatibility::hoi3::content::ProvinceDefinitionId provinceId
            : country.coreProvinces)
        {
            const ProvinceState* province = world.FindProvince(provinceId);
            if (province == nullptr
                || province->cores.count(country.id) == 0)
            {
                report.Error(
                    "worldbuilder.territory_core_forward_missing",
                    "Country core index is inconsistent for Province "
                        + ProvinceIdText(provinceId)
                );
                valid = false;
            }
        }
    }
    return valid;
}

bool WorldBuilder::InstantiateRuntimeUnits(
    const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
    Hoi3WorldState& world,
    WorldBuildReport& report
)
{
    world.units_.clear();
    for (ProvinceState& province : world.provinces_)
    {
        province.locatedUnits.clear();
        province.basedUnits.clear();
    }

    bool success = true;
    for (CountryState& country : world.countries_)
    {
        country.unitRoots.clear();
        for (const OrderOfBattleSelection& selection
            : country.ordersOfBattle)
        {
            if (!selection.definition)
            {
                continue;
            }
            const dillen::compatibility::hoi3::content::OrderOfBattleDefinition* source =
                definitions.OrdersOfBattle().Find(*selection.definition);
            if (source == nullptr)
            {
                report.Error(
                    "worldbuilder.runtime_oob_missing",
                    "Runtime unit assembly references a missing OOB Definition"
                );
                success = false;
                continue;
            }
            if (!source->referencesResolved)
            {
                report.Error(
                    "worldbuilder.runtime_oob_unresolved",
                    "Runtime unit assembly requires a resolved OOB Definition",
                    source->origin
                );
                success = false;
                continue;
            }
            for (const dillen::compatibility::hoi3::content::OrderOfBattleNode& root : source->roots)
            {
                const std::optional<RuntimeUnitId> rootId =
                    InstantiateRuntimeUnitNode(
                        definitions,
                        *source,
                        root,
                        country.id,
                        std::nullopt,
                        std::nullopt,
                        std::nullopt,
                        world,
                        report
                    );
                if (!rootId)
                {
                    success = false;
                    continue;
                }
                country.unitRoots.push_back(*rootId);
            }
        }
    }
    return success;
}

std::optional<RuntimeUnitId> WorldBuilder::InstantiateRuntimeUnitNode(
    const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
    const dillen::compatibility::hoi3::content::OrderOfBattleDefinition& source,
    const dillen::compatibility::hoi3::content::OrderOfBattleNode& node,
    dillen::compatibility::hoi3::content::CountryDefinitionId country,
    std::optional<RuntimeUnitId> parent,
    std::optional<dillen::compatibility::hoi3::content::ProvinceDefinitionId> inheritedLocation,
    std::optional<dillen::compatibility::hoi3::content::ProvinceDefinitionId> inheritedBase,
    Hoi3WorldState& world,
    WorldBuildReport& report
)
{
    const std::optional<dillen::compatibility::hoi3::content::ProvinceDefinitionId> location =
        node.location ? node.location : inheritedLocation;
    const std::optional<dillen::compatibility::hoi3::content::ProvinceDefinitionId> base =
        node.base ? node.base : inheritedBase;
    ProvinceState* locationState = location
        ? world.FindProvinceMutable(*location)
        : nullptr;
    ProvinceState* baseState = base
        ? world.FindProvinceMutable(*base)
        : nullptr;
    if (location && locationState == nullptr)
    {
        report.Error(
            "worldbuilder.runtime_unit_location_missing",
            "Runtime unit references missing Province ID "
                + ProvinceIdText(*location),
            node.origin
        );
        return std::nullopt;
    }
    if (base && baseState == nullptr)
    {
        report.Error(
            "worldbuilder.runtime_unit_base_missing",
            "Runtime unit base references missing Province ID "
                + ProvinceIdText(*base),
            node.origin
        );
        return std::nullopt;
    }
    if (node.unitType
        && definitions.UnitTypes().Find(*node.unitType) == nullptr)
    {
        report.Error(
            "worldbuilder.runtime_unit_type_missing",
            "Runtime unit references a missing UnitType Definition",
            node.origin
        );
        return std::nullopt;
    }
    if (node.expeditionaryOwner
        && world.FindCountry(*node.expeditionaryOwner) == nullptr)
    {
        report.Error(
            "worldbuilder.runtime_unit_expeditionary_owner_missing",
            "Runtime unit references a missing expeditionary owner",
            node.origin
        );
        return std::nullopt;
    }

    RuntimeUnitState unit;
    unit.id = {
        static_cast<std::uint64_t>(world.units_.size()) + 1
    };
    unit.source = source.id;
    unit.kind = node.kind;
    unit.name = node.name;
    unit.unitType = node.unitType;
    unit.country = country;
    unit.expeditionaryOwner = node.expeditionaryOwner;
    unit.location = location;
    unit.base = base;
    unit.leader = node.leader;
    unit.parent = parent;
    const RuntimeUnitId id = unit.id;
    world.units_.push_back(std::move(unit));
    if (locationState != nullptr)
    {
        locationState->locatedUnits.push_back(id);
    }
    if (baseState != nullptr)
    {
        baseState->basedUnits.push_back(id);
    }

    for (const dillen::compatibility::hoi3::content::OrderOfBattleNode& child : node.children)
    {
        const std::optional<RuntimeUnitId> childId =
            InstantiateRuntimeUnitNode(
                definitions,
                source,
                child,
                country,
                id,
                location,
                base,
                world,
                report
            );
        if (!childId)
        {
            return std::nullopt;
        }
        RuntimeUnitState* parentState = world.FindUnitMutable(id);
        if (parentState == nullptr)
        {
            report.Error(
                "worldbuilder.runtime_unit_parent_missing",
                "Runtime unit parent could not be recovered during assembly",
                node.origin
            );
            return std::nullopt;
        }
        parentState->children.push_back(*childId);
    }
    return id;
}

bool WorldBuilder::BuildBookmark(
    const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
    dillen::compatibility::hoi3::content::BookmarkDefinitionId bookmark,
    Hoi3WorldState& output,
    WorldBuildReport& report
) const
{
    return BuildBookmarkInternal(
        definitions,
        nullptr,
        bookmark,
        output,
        report
    );
}

bool WorldBuilder::BuildBookmark(
    const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
    const kernel::FrozenRuntimeCatalog& runtimeCatalog,
    dillen::compatibility::hoi3::content::BookmarkDefinitionId bookmark,
    Hoi3WorldState& output,
    WorldBuildReport& report
) const
{
    return BuildBookmarkInternal(
        definitions,
        &runtimeCatalog,
        bookmark,
        output,
        report
    );
}

bool WorldBuilder::BuildBookmarkInternal(
    const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
    const kernel::FrozenRuntimeCatalog* runtimeCatalog,
    dillen::compatibility::hoi3::content::BookmarkDefinitionId bookmark,
    Hoi3WorldState& output,
    WorldBuildReport& report
) const
{
    if (!definitions.IsFrozen())
    {
        report.Clear();
        report.Error(
            "worldbuilder.registry_not_frozen",
            "Definition Registry must be frozen before world construction"
        );
        return false;
    }
    const dillen::compatibility::hoi3::content::BookmarkDefinition* definition =
        definitions.Launches().Find(bookmark);
    if (definition == nullptr)
    {
        report.Clear();
        report.Error(
            "worldbuilder.bookmark_missing",
            "Bookmark Definition is not present in the frozen Registry"
        );
        return false;
    }
    Hoi3WorldState candidate;
    if (!BuildInternal(
        definitions,
        runtimeCatalog,
        definition->date,
        candidate,
        report))
    {
        return false;
    }
    candidate.bookmark_ = bookmark;
    output = std::move(candidate);
    return true;
}

bool WorldBuilder::BuildScenario(
    const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
    dillen::compatibility::hoi3::content::ScenarioDefinitionId scenario,
    Hoi3WorldState& output,
    WorldBuildReport& report
) const
{
    return BuildScenarioInternal(
        definitions,
        nullptr,
        scenario,
        output,
        report
    );
}

bool WorldBuilder::BuildScenario(
    const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
    const kernel::FrozenRuntimeCatalog& runtimeCatalog,
    dillen::compatibility::hoi3::content::ScenarioDefinitionId scenario,
    Hoi3WorldState& output,
    WorldBuildReport& report
) const
{
    return BuildScenarioInternal(
        definitions,
        &runtimeCatalog,
        scenario,
        output,
        report
    );
}

bool WorldBuilder::BuildScenarioInternal(
    const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
    const kernel::FrozenRuntimeCatalog* runtimeCatalog,
    dillen::compatibility::hoi3::content::ScenarioDefinitionId scenario,
    Hoi3WorldState& output,
    WorldBuildReport& report
) const
{
    if (!definitions.IsFrozen())
    {
        report.Clear();
        report.Error(
            "worldbuilder.registry_not_frozen",
            "Definition Registry must be frozen before world construction"
        );
        return false;
    }
    const dillen::compatibility::hoi3::content::ScenarioDefinition* definition =
        definitions.Launches().Find(scenario);
    if (definition == nullptr)
    {
        report.Clear();
        report.Error(
            "worldbuilder.scenario_missing",
            "Scenario Definition is not present in the frozen Registry"
        );
        return false;
    }
    Hoi3WorldState candidate;
    if (!BuildInternal(
        definitions,
        runtimeCatalog,
        definition->startDate,
        candidate,
        report))
    {
        return false;
    }
    candidate.scenario_ = scenario;
    output = std::move(candidate);
    return true;
}

bool WorldBuilder::ApplyCountryOperation(
    const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
    const dillen::compatibility::hoi3::content::CountryHistoryOperation& operation,
    CountryState& country,
    Hoi3WorldState& world,
    WorldBuildReport& report
)
{
    using dillen::compatibility::hoi3::content::CountryHistoryField;
    const auto typeError = [&report, &operation]()
    {
        report.Error(
            "worldbuilder.country_history_value_invalid",
            "Country history value has an invalid resolved type",
            operation.origin
        );
        return false;
    };
    const auto readString = [&operation]() -> const std::string*
    {
        return std::get_if<std::string>(&operation.value);
    };

    switch (operation.field)
    {
    case CountryHistoryField::Capital:
    {
        const auto* value = std::get_if<dillen::compatibility::hoi3::content::ProvinceDefinitionId>(
            &operation.value
        );
        if (value == nullptr)
        {
            return typeError();
        }
        if (definitions.Provinces().Find(*value) == nullptr)
        {
            report.Error(
                "worldbuilder.country_capital_missing",
                "Country capital references missing Province ID "
                    + ProvinceIdText(*value),
                operation.origin
            );
            return false;
        }
        country.capital = *value;
        return true;
    }
    case CountryHistoryField::Government:
    {
        const std::string* value = readString();
        if (value == nullptr)
        {
            return typeError();
        }
        country.government = *value;
        return true;
    }
    case CountryHistoryField::Ideology:
    {
        const std::string* value = readString();
        if (value == nullptr)
        {
            return typeError();
        }
        country.ideology = *value;
        return true;
    }
    case CountryHistoryField::Minister:
    {
        const auto* value = std::get_if<std::int64_t>(&operation.value);
        if (value == nullptr || *value < 0)
        {
            return typeError();
        }
        country.ministers[operation.key] = static_cast<std::uint64_t>(*value);
        return true;
    }
    case CountryHistoryField::Alignment:
    {
        const auto* value = std::get_if<dillen::compatibility::hoi3::content::CountryAlignment>(
            &operation.value
        );
        if (value == nullptr)
        {
            return typeError();
        }
        country.alignment = *value;
        return true;
    }
    case CountryHistoryField::Neutrality:
    case CountryHistoryField::NationalUnity:
    case CountryHistoryField::OfficersRatio:
    case CountryHistoryField::Threat:
    case CountryHistoryField::SetManpower:
    {
        const std::optional<double> value = ReadNumber(operation.value);
        if (!value)
        {
            return typeError();
        }
        if (operation.field == CountryHistoryField::Neutrality)
        {
            country.neutrality = *value;
        }
        else if (operation.field == CountryHistoryField::NationalUnity)
        {
            country.nationalUnity = *value;
        }
        else if (operation.field == CountryHistoryField::OfficersRatio)
        {
            country.officersRatio = *value;
        }
        else if (operation.field == CountryHistoryField::Threat)
        {
            country.threat = *value;
        }
        else
        {
            country.manpower = *value;
        }
        return true;
    }
    case CountryHistoryField::OrderOfBattle:
    case CountryHistoryField::LoadOrderOfBattle:
    {
        OrderOfBattleSelection selection;
        selection.additive = operation.field
            == CountryHistoryField::LoadOrderOfBattle;
        if (const auto* value =
            std::get_if<dillen::compatibility::hoi3::content::OrderOfBattleDefinitionId>(&operation.value))
        {
            if (definitions.OrdersOfBattle().Find(*value) == nullptr)
            {
                report.Error(
                    "worldbuilder.oob_definition_missing",
                    "Country history references missing OOB Definition",
                    operation.origin
                );
                return false;
            }
            selection.definition = *value;
        }
        else if (const std::string* value = readString())
        {
            selection.unresolvedPath = *value;
            report.Warning(
                "worldbuilder.oob_reference_unresolved",
                "OOB reference remains unresolved: " + *value,
                operation.origin
            );
        }
        else
        {
            return typeError();
        }
        if (!selection.additive)
        {
            country.ordersOfBattle.clear();
        }
        country.ordersOfBattle.push_back(std::move(selection));
        return true;
    }
    case CountryHistoryField::Popularity:
    case CountryHistoryField::Organization:
    {
        const auto* value =
            std::get_if<dillen::compatibility::hoi3::content::CountryHistoryNamedNumberMap>(
                &operation.value
            );
        if (value == nullptr)
        {
            return typeError();
        }
        std::map<std::string, double>& target = operation.field
            == CountryHistoryField::Popularity
            ? country.popularity
            : country.organization;
        target.clear();
        for (const dillen::compatibility::hoi3::content::CountryHistoryNamedNumber& item : value->values)
        {
            target[item.name] = item.value;
        }
        return true;
    }
    case CountryHistoryField::SetCountryFlag:
    case CountryHistoryField::SetGlobalFlag:
    case CountryHistoryField::JoinFaction:
    case CountryHistoryField::LeaveFaction:
    case CountryHistoryField::Decision:
    {
        const std::string* value = readString();
        if (value == nullptr)
        {
            return typeError();
        }
        if (operation.field == CountryHistoryField::SetCountryFlag)
        {
            country.flags.insert(*value);
        }
        else if (operation.field == CountryHistoryField::SetGlobalFlag)
        {
            world.globalFlags_.insert(*value);
        }
        else if (operation.field == CountryHistoryField::JoinFaction)
        {
            country.faction = *value;
        }
        else if (operation.field == CountryHistoryField::LeaveFaction)
        {
            country.faction.reset();
        }
        else
        {
            country.decisions.insert(*value);
        }
        return true;
    }
    case CountryHistoryField::CreateAlliance:
    {
        const auto* value = std::get_if<dillen::compatibility::hoi3::content::CountryDefinitionId>(
            &operation.value
        );
        if (value == nullptr)
        {
            return typeError();
        }
        CountryState* other = world.FindCountryMutable(*value);
        if (other == nullptr)
        {
            report.Error(
                "worldbuilder.alliance_country_missing",
                "Alliance references missing Country ID "
                    + CountryIdText(*value),
                operation.origin
            );
            return false;
        }
        country.alliances.insert(*value);
        other->alliances.insert(country.id);
        return true;
    }
    case CountryHistoryField::NamedAssignment:
    {
        if (const auto* value = std::get_if<std::int64_t>(&operation.value))
        {
            country.namedAssignments[operation.key] = *value;
        }
        else if (const auto* value = std::get_if<double>(&operation.value))
        {
            country.namedAssignments[operation.key] = *value;
        }
        else if (const auto* value = std::get_if<bool>(&operation.value))
        {
            country.namedAssignments[operation.key] = *value;
        }
        else if (const std::string* value = readString())
        {
            country.namedAssignments[operation.key] = *value;
        }
        else
        {
            return typeError();
        }
        return true;
    }
    case CountryHistoryField::TechnologyLevel:
    {
        const auto* value =
            std::get_if<dillen::compatibility::hoi3::content::CountryHistoryTechnologyLevel>(
                &operation.value
            );
        if (value == nullptr)
        {
            return typeError();
        }
        if (definitions.Technologies().Find(value->technology) == nullptr)
        {
            report.Error(
                "worldbuilder.technology_missing",
                "Country history references missing Technology Definition",
                operation.origin
            );
            return false;
        }
        country.technologies[value->technology] = value->level;
        return true;
    }
    }
    return typeError();
}

bool WorldBuilder::ApplyProvinceOperation(
    const dillen::compatibility::hoi3::content::DefinitionRegistry& definitions,
    const dillen::compatibility::hoi3::content::ProvinceHistoryOperation& operation,
    ProvinceState& province,
    WorldBuildReport& report
)
{
    using dillen::compatibility::hoi3::content::ProvinceHistoryField;
    const auto typeError = [&report, &operation]()
    {
        report.Error(
            "worldbuilder.province_history_value_invalid",
            "Province history value has an invalid resolved type",
            operation.origin
        );
        return false;
    };

    if (operation.field == ProvinceHistoryField::Owner
        || operation.field == ProvinceHistoryField::Controller
        || operation.field == ProvinceHistoryField::AddCore
        || operation.field == ProvinceHistoryField::RemoveCore)
    {
        const auto* value = std::get_if<dillen::compatibility::hoi3::content::CountryDefinitionId>(
            &operation.value
        );
        if (value == nullptr)
        {
            return typeError();
        }
        if (definitions.Countries().Find(*value) == nullptr)
        {
            report.Error(
                "worldbuilder.province_country_missing",
                "Province history references missing Country ID "
                    + CountryIdText(*value),
                operation.origin
            );
            return false;
        }
        if (operation.field == ProvinceHistoryField::Owner)
        {
            province.owner = *value;
        }
        else if (operation.field == ProvinceHistoryField::Controller)
        {
            province.controller = *value;
        }
        else if (operation.field == ProvinceHistoryField::AddCore)
        {
            province.cores.insert(*value);
        }
        else
        {
            province.cores.erase(*value);
        }
        return true;
    }
    if (operation.field == ProvinceHistoryField::Terrain
        || operation.field == ProvinceHistoryField::StrategicResource)
    {
        const auto* value = std::get_if<std::string>(&operation.value);
        if (value == nullptr)
        {
            return typeError();
        }
        if (operation.field == ProvinceHistoryField::Terrain)
        {
            province.terrain = *value;
        }
        else
        {
            province.strategicResource = *value;
        }
        return true;
    }
    if (IsProvinceNumericField(operation.field))
    {
        const std::optional<double> value = ReadNumber(operation.value);
        if (!value)
        {
            return typeError();
        }
        province.numericValues[operation.field] = *value;
        return true;
    }
    return typeError();
}

}
