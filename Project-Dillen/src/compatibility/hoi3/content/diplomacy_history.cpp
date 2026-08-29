#include "diplomacy_history.hpp"

#include <tuple>
#include <utility>

namespace dillen::compatibility::hoi3::content {

bool operator==(
    const DiplomacyHistoryKey& first,
    const DiplomacyHistoryKey& second
) noexcept
{
    return first.kind == second.kind
        && first.first == second.first
        && first.second == second.second;
}

bool operator!=(
    const DiplomacyHistoryKey& first,
    const DiplomacyHistoryKey& second
) noexcept
{
    return !(first == second);
}

bool operator<(
    const DiplomacyHistoryKey& first,
    const DiplomacyHistoryKey& second
) noexcept
{
    return std::tie(first.kind, first.first, first.second)
        < std::tie(second.kind, second.first, second.second);
}

DiplomacyHistoryKey CanonicalDiplomacyHistoryKey(
    DiplomaticRelationKind kind,
    CountryDefinitionId first,
    CountryDefinitionId second
) noexcept
{
    if (kind == DiplomaticRelationKind::Alliance && second < first)
    {
        std::swap(first, second);
    }
    return {kind, first, second};
}

}
