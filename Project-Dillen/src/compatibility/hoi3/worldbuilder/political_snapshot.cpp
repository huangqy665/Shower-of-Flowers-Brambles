#include "political_snapshot.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>

#include "country_history.hpp"
#include "province_history.hpp"

namespace dillen::compatibility::hoi3::worldbuilder {

namespace {

// A CountryDefinitionId is the tag's three characters packed big-endian by
// CountryTag::StableId. Reporting the packed integer means a reader has to
// decode 4997721 by hand to learn it is LBY, so the messages below name the
// tag and keep the number beside it.
std::string DescribeCountry(content::CountryDefinitionId id)
{
    char characters[3] = {
        static_cast<char>((id.value >> 16) & 0xFFu),
        static_cast<char>((id.value >> 8) & 0xFFu),
        static_cast<char>(id.value & 0xFFu)
    };
    std::string text;
    for (const char character : characters)
    {
        // A tag that is not printable is not a tag; say so rather than
        // emitting control bytes into a diagnostic.
        if (character < 0x20 || character > 0x7E)
        {
            return std::to_string(id.value);
        }
        text.push_back(character);
    }
    return text + " (" + std::to_string(id.value) + ")";
}

bool DateIsAtOrBefore(
    content::DefinitionDate first,
    content::DefinitionDate second
)
{
    return !(second < first);
}

bool IsValidDate(
    content::DefinitionDate date
) noexcept
{
    if (date.year < 1
        || date.month < 1
        || date.month > 12
        || date.day < 1)
    {
        return false;
    }

    static constexpr int kDaysPerMonth[] = {
        31, 28, 31, 30, 31, 30,
        31, 31, 30, 31, 30, 31
    };

    int days = kDaysPerMonth[date.month - 1];

    const bool leapYear =
        date.year % 4 == 0
        && (date.year % 100 != 0
            || date.year % 400 == 0);

    if (date.month == 2 && leapYear)
    {
        ++days;
    }

    return date.day <= days;
}

bool CountryExists(
    const content::DefinitionRegistry& definitions,
    content::CountryDefinitionId country
)
{
    return definitions.Countries().Find(country) != nullptr;
}

bool ProvinceExists(
    const content::DefinitionRegistry& definitions,
    content::ProvinceDefinitionId province
)
{
    return definitions.Provinces().Find(province) != nullptr;
}

bool ApplyCountryOperation(
    const content::DefinitionRegistry& definitions,
    const content::CountryHistoryOperation& operation,
    PoliticalCountryState& country,
    PoliticalSnapshotReport& report
)
{
    using content::CountryHistoryField;

    //
    // First slice deliberately projects only the part of Country History
    // needed to build the political map.
    //
    // Government / ideology / law / minister / technology / OOB remain in the
    // HOI3 Definition Registry. They are not discarded; they simply do not
    // have Dillen target semantics in this milestone.
    //
    if (operation.field != CountryHistoryField::Capital)
    {
        return true;
    }

    const auto* capital =
        std::get_if<content::ProvinceDefinitionId>(
            &operation.value
        );

    if (capital == nullptr)
    {
        report.Error(
            "hoi3.political_snapshot.country_capital_type_invalid",
            "Country capital does not contain a resolved Province ID",
            operation.origin
        );
        return false;
    }

    if (!ProvinceExists(
            definitions,
            *capital))
    {
        report.Error(
            "hoi3.political_snapshot.country_capital_missing",
            "Country capital references Province ID "
                + std::to_string(capital->value)
                + ", which is absent from the active province definitions",
            operation.origin
        );
        return false;
    }

    country.capital = *capital;

    return true;
}

bool ApplyProvinceOperation(
    const content::DefinitionRegistry& definitions,
    const content::ProvinceHistoryOperation& operation,
    PoliticalProvinceState& province,
    PoliticalSnapshotReport& report
)
{
    using content::ProvinceHistoryField;

    //
    // Terrain, resources, infrastructure, IC and bases are deliberately left
    // untouched here. They belong to later Dillen domains.
    //
    if (operation.field != ProvinceHistoryField::Owner
        && operation.field != ProvinceHistoryField::Controller
        && operation.field != ProvinceHistoryField::AddCore
        && operation.field != ProvinceHistoryField::RemoveCore)
    {
        return true;
    }

    const auto* country =
        std::get_if<content::CountryDefinitionId>(
            &operation.value
        );

    if (country == nullptr)
    {
        report.Error(
            "hoi3.political_snapshot.province_country_type_invalid",
            "Province political history does not contain a resolved Country ID",
            operation.origin
        );
        return false;
    }

    if (!CountryExists(
            definitions,
            *country))
    {
        // Skipped, not fatal.
        //
        // A corpus under construction carries province history for countries
        // it does not yet define -- the French and Italian colonies are in
        // every province file whether or not the scenario has reached them.
        // Refusing the whole snapshot for one of those means no world at all
        // rather than a world without that country, which is the wrong
        // trade for content that is meant to be filled in over time.
        //
        // The operation is dropped: the province simply keeps whatever owner,
        // controller or core state it already had. Reported once per
        // occurrence so an unfinished tag stays visible rather than silent.
        report.Warning(
            "hoi3.political_snapshot.province_country_missing",
            "Province political history references Country "
                + DescribeCountry(*country)
                + ", which has no definition; the operation is skipped",
            operation.origin
        );
        return true;
    }

    switch (operation.field)
    {
    case ProvinceHistoryField::Owner:

        province.owner = *country;
        return true;

    case ProvinceHistoryField::Controller:

        province.controller = *country;
        return true;

    case ProvinceHistoryField::AddCore:

        if (std::find(
                province.cores.begin(),
                province.cores.end(),
                *country)
            == province.cores.end())
        {
            province.cores.push_back(*country);
        }

        return true;

    case ProvinceHistoryField::RemoveCore:

        province.cores.erase(
            std::remove(
                province.cores.begin(),
                province.cores.end(),
                *country),
            province.cores.end()
        );

        return true;

    default:
        return true;
    }
}

}

void PoliticalSnapshotReport::Clear()
{
    issues.clear();
}

void PoliticalSnapshotReport::Warning(
    std::string code,
    std::string message,
    content::DefinitionOrigin origin
)
{
    issues.push_back({
        PoliticalSnapshotIssueSeverity::Warning,
        std::move(code),
        std::move(message),
        std::move(origin)
    });
}

void PoliticalSnapshotReport::Error(
    std::string code,
    std::string message,
    content::DefinitionOrigin origin
)
{
    issues.push_back({
        PoliticalSnapshotIssueSeverity::Error,
        std::move(code),
        std::move(message),
        std::move(origin)
    });
}

bool PoliticalSnapshotReport::HasErrors() const noexcept
{
    return ErrorCount() != 0;
}

std::size_t PoliticalSnapshotReport::WarningCount() const noexcept
{
    return static_cast<std::size_t>(
        std::count_if(
            issues.begin(),
            issues.end(),
            [](const PoliticalSnapshotIssue& issue)
            {
                return issue.severity
                    == PoliticalSnapshotIssueSeverity::Warning;
            }
        )
    );
}

std::size_t PoliticalSnapshotReport::ErrorCount() const noexcept
{
    return static_cast<std::size_t>(
        std::count_if(
            issues.begin(),
            issues.end(),
            [](const PoliticalSnapshotIssue& issue)
            {
                return issue.severity
                    == PoliticalSnapshotIssueSeverity::Error;
            }
        )
    );
}

bool BuildPoliticalSnapshot(
    const content::DefinitionRegistry& definitions,
    content::DefinitionDate date,
    PoliticalSnapshot& output,
    PoliticalSnapshotReport& report
)
{
    report.Clear();
    output = {};

    if (!definitions.IsFrozen())
    {
        report.Error(
            "hoi3.political_snapshot.registry_not_frozen",
            "Definition Registry must be frozen before a political snapshot "
            "is built"
        );
        return false;
    }

    if (!IsValidDate(date))
    {
        report.Error(
            "hoi3.political_snapshot.date_invalid",
            "Political snapshot date is not a valid Gregorian date"
        );
        return false;
    }

    PoliticalSnapshot candidate;
    candidate.date = date;

    //
    // ---------------------------------------------------------------------
    // Countries
    // ---------------------------------------------------------------------
    //
    // For this first milestone every declared Country Tag gets a Dillen-side
    // candidate identity.
    //
    // This deliberately preserves core claims made by tags which may not own
    // territory at the selected date. A later static-definition pass can
    // separate "possible country definition" from "active country entity"
    // once real gameplay semantics require that distinction.
    //
    std::map<
        content::CountryDefinitionId,
        std::size_t
    > countryIndex;

    candidate.countries.reserve(
        definitions.Countries().Size()
    );

    for (const content::CountryTagDefinition& definition
        : definitions.Countries().All())
    {
        PoliticalCountryState country;

        country.id = definition.id;
        country.tag = definition.tag;

        //
        // Country colour is source presentation data. It is carried through
        // this compatibility value only so the emitter can place it into a
        // Presentation Package. It never becomes Authoritative World state.
        //
        if (definition.definition
            && definition.definition->color)
        {
            country.color =
                *definition.definition->color;
        }

        countryIndex.emplace(
            country.id,
            candidate.countries.size()
        );

        candidate.countries.push_back(
            std::move(country)
        );
    }

    //
    // ---------------------------------------------------------------------
    // Provinces
    // ---------------------------------------------------------------------
    //
    std::map<
        content::ProvinceDefinitionId,
        std::size_t
    > provinceIndex;

    candidate.provinces.reserve(
        definitions.Provinces().Size()
    );

    for (const content::ProvinceDefinition& definition
        : definitions.Provinces().All())
    {
        PoliticalProvinceState province;

        province.id = definition.id;

        provinceIndex.emplace(
            province.id,
            candidate.provinces.size()
        );

        candidate.provinces.push_back(
            std::move(province)
        );
    }

    //
    // ---------------------------------------------------------------------
    // Country History -> selected-date country state
    // ---------------------------------------------------------------------
    //
    for (const content::CountryHistoryTimeline& timeline
        : definitions.CountryHistories().All())
    {
        const auto target =
            countryIndex.find(timeline.country);

        if (target == countryIndex.end())
        {
            // Same reason as the province case above: a history file for a
            // tag the tag index does not list is content that is not ready,
            // not a corpus that cannot be read. The timeline is skipped.
            report.Warning(
                "hoi3.political_snapshot.country_history_target_missing",
                "Country history targets Country "
                    + DescribeCountry(timeline.country)
                    + ", which is absent from common/countries.txt; the "
                      "timeline is skipped"
            );
            continue;
        }

        PoliticalCountryState& country =
            candidate.countries[target->second];

        //
        // Base state.
        //
        for (const content::CountryHistoryOperation& operation
            : timeline.initialOperations)
        {
            ApplyCountryOperation(
                definitions,
                operation,
                country,
                report
            );
        }

        //
        // Historical patches up to the selected bookmark date.
        //
        for (const content::CountryHistoryPatch& patch
            : timeline.patches)
        {
            if (!DateIsAtOrBefore(
                    patch.date,
                    date))
            {
                break;
            }

            for (const content::CountryHistoryOperation& operation
                : patch.operations)
            {
                ApplyCountryOperation(
                    definitions,
                    operation,
                    country,
                    report
                );
            }
        }
    }

    //
    // ---------------------------------------------------------------------
    // Province History -> selected-date territorial state
    // ---------------------------------------------------------------------
    //
    for (const content::ProvinceHistoryTimeline& timeline
        : definitions.ProvinceHistories().All())
    {
        const auto target =
            provinceIndex.find(timeline.province);

        if (target == provinceIndex.end())
        {
            report.Error(
                "hoi3.political_snapshot.province_history_target_missing",
                "Province history targets Province ID "
                    + std::to_string(timeline.province.value)
                    + ", which is absent from map/definition.csv"
            );
            continue;
        }

        PoliticalProvinceState& province =
            candidate.provinces[target->second];

        //
        // Base state.
        //
        for (const content::ProvinceHistoryOperation& operation
            : timeline.initialOperations)
        {
            ApplyProvinceOperation(
                definitions,
                operation,
                province,
                report
            );
        }

        //
        // Historical patches <= target date.
        //
        for (const content::ProvinceHistoryPatch& patch
            : timeline.patches)
        {
            if (!DateIsAtOrBefore(
                    patch.date,
                    date))
            {
                break;
            }

            for (const content::ProvinceHistoryOperation& operation
                : patch.operations)
            {
                ApplyProvinceOperation(
                    definitions,
                    operation,
                    province,
                    report
                );
            }
        }

        //
        // Relation tables must be deterministic regardless of the ordering in
        // which add_core appeared in the source files.
        //
        std::sort(
            province.cores.begin(),
            province.cores.end()
        );

        province.cores.erase(
            std::unique(
                province.cores.begin(),
                province.cores.end()
            ),
            province.cores.end()
        );
    }

    if (report.HasErrors())
    {
        return false;
    }

    output = std::move(candidate);

    return true;
}

}