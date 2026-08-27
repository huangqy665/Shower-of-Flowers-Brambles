#include "unit_type_definition.hpp"

namespace dillen::content {

UnitTypeDefinitionId::operator bool() const noexcept
{
    return value != 0;
}

bool operator==(
    UnitTypeDefinitionId first,
    UnitTypeDefinitionId second
) noexcept
{
    return first.value == second.value;
}

bool operator!=(
    UnitTypeDefinitionId first,
    UnitTypeDefinitionId second
) noexcept
{
    return !(first == second);
}

bool operator<(
    UnitTypeDefinitionId first,
    UnitTypeDefinitionId second
) noexcept
{
    return first.value < second.value;
}

std::string NormalizeUnitTypeName(std::string_view name)
{
    std::string normalized(name);
    for (char& character : normalized)
    {
        if (character >= 'A' && character <= 'Z')
        {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return normalized;
}

UnitTypeDefinitionId StableUnitTypeDefinitionId(
    std::string_view name
) noexcept
{
    constexpr std::uint64_t kOffset = 14695981039346656037ULL;
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    std::uint64_t value = kOffset;
    for (unsigned char character : name)
    {
        if (character >= 'A' && character <= 'Z')
        {
            character = static_cast<unsigned char>(
                character - 'A' + 'a'
            );
        }
        value ^= character;
        value *= kPrime;
    }
    return {value};
}

}
