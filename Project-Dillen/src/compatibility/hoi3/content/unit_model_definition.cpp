#include "unit_model_definition.hpp"

namespace dillen::compatibility::hoi3::content {

namespace {

void HashByte(std::uint64_t& value, unsigned char byte) noexcept
{
    constexpr std::uint64_t kPrime = 1099511628211ULL;
    value ^= byte;
    value *= kPrime;
}

}

UnitModelDefinitionId::operator bool() const noexcept
{
    return value != 0;
}

bool operator==(
    UnitModelDefinitionId first,
    UnitModelDefinitionId second
) noexcept
{
    return first.value == second.value;
}

bool operator!=(
    UnitModelDefinitionId first,
    UnitModelDefinitionId second
) noexcept
{
    return !(first == second);
}

bool operator<(
    UnitModelDefinitionId first,
    UnitModelDefinitionId second
) noexcept
{
    return first.value < second.value;
}

UnitModelDefinitionId StableUnitModelDefinitionId(
    CountryDefinitionId country,
    std::string_view unitTypeName,
    int modelIndex
) noexcept
{
    constexpr std::uint64_t kOffset = 14695981039346656037ULL;
    std::uint64_t value = kOffset;
    for (int shift = 0; shift < 32; shift += 8)
    {
        HashByte(
            value,
            static_cast<unsigned char>((country.value >> shift) & 0xFFU)
        );
    }
    for (unsigned char character : unitTypeName)
    {
        if (character >= 'A' && character <= 'Z')
        {
            character = static_cast<unsigned char>(
                character - 'A' + 'a'
            );
        }
        HashByte(value, character);
    }
    HashByte(value, 0);
    const std::uint32_t index = static_cast<std::uint32_t>(modelIndex);
    for (int shift = 0; shift < 32; shift += 8)
    {
        HashByte(
            value,
            static_cast<unsigned char>((index >> shift) & 0xFFU)
        );
    }
    return {value};
}

}
