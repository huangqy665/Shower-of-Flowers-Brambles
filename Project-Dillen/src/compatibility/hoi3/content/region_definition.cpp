#include "region_definition.hpp"

#include <algorithm>

namespace dillen::compatibility::hoi3::content {

RegionDefinitionId::operator bool() const noexcept
{
    return value != 0;
}

bool operator==(
    RegionDefinitionId first,
    RegionDefinitionId second
) noexcept
{
    return first.value == second.value;
}

bool operator!=(
    RegionDefinitionId first,
    RegionDefinitionId second
) noexcept
{
    return !(first == second);
}

bool operator<(
    RegionDefinitionId first,
    RegionDefinitionId second
) noexcept
{
    return first.value < second.value;
}

RegionDefinitionId StableRegionDefinitionId(
    std::string_view name
) noexcept
{
    constexpr std::uint64_t kOffset = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t value = kOffset;
    for (const unsigned char character : name)
    {
        value ^= character;
        value *= kPrime;
    }
    return {value};
}

bool RegionDefinition::HasFlag(std::string_view flag) const noexcept
{
    return std::find(flags.begin(), flags.end(), flag) != flags.end();
}

}
