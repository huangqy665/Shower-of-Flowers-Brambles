#include "definition_date.hpp"

#include <tuple>

namespace dillen::content {

bool operator==(
    const DefinitionDate& first,
    const DefinitionDate& second
) noexcept
{
    return first.year == second.year
        && first.month == second.month
        && first.day == second.day;
}

bool operator!=(
    const DefinitionDate& first,
    const DefinitionDate& second
) noexcept
{
    return !(first == second);
}

bool operator<(
    const DefinitionDate& first,
    const DefinitionDate& second
) noexcept
{
    return std::tie(first.year, first.month, first.day)
        < std::tie(second.year, second.month, second.day);
}

}
