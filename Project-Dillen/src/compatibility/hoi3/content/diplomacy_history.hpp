#pragma once

#include <cstdint>
#include <vector>

#include "country_tag_definition.hpp"
#include "definition_date.hpp"
#include "definition_origin.hpp"

namespace dillen::compatibility::hoi3::content {

enum class DiplomaticRelationKind
{
    Alliance,
    Guarantee,
    Vassal
};

struct DiplomacyHistoryKey
{
    DiplomaticRelationKind kind = DiplomaticRelationKind::Alliance;
    CountryDefinitionId first;
    CountryDefinitionId second;
};

bool operator==(
    const DiplomacyHistoryKey& first,
    const DiplomacyHistoryKey& second
) noexcept;
bool operator!=(
    const DiplomacyHistoryKey& first,
    const DiplomacyHistoryKey& second
) noexcept;
bool operator<(
    const DiplomacyHistoryKey& first,
    const DiplomacyHistoryKey& second
) noexcept;

DiplomacyHistoryKey CanonicalDiplomacyHistoryKey(
    DiplomaticRelationKind kind,
    CountryDefinitionId first,
    CountryDefinitionId second
) noexcept;

struct DiplomaticRelationPeriod
{
    DefinitionDate startDate;
    DefinitionDate endDate;
    DefinitionOrigin origin;
    std::uint64_t sequence = 0;
};

struct DiplomacyHistoryTimeline
{
    DiplomacyHistoryKey key;
    std::vector<DiplomaticRelationPeriod> periods;
};

}
