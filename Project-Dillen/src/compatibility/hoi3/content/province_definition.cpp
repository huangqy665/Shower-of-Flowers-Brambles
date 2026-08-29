#include "province_definition.hpp"

namespace dillen::compatibility::hoi3::content {

ProvinceDefinitionId::operator bool() const noexcept
{
    return value != 0;
}

bool operator==(
    ProvinceDefinitionId first,
    ProvinceDefinitionId second
) noexcept
{
    return first.value == second.value;
}

bool operator!=(
    ProvinceDefinitionId first,
    ProvinceDefinitionId second
) noexcept
{
    return !(first == second);
}

bool operator<(
    ProvinceDefinitionId first,
    ProvinceDefinitionId second
) noexcept
{
    return first.value < second.value;
}

std::uint32_t ProvinceColor::PackedRgb() const noexcept
{
    return (static_cast<std::uint32_t>(red) << 16U)
        | (static_cast<std::uint32_t>(green) << 8U)
        | static_cast<std::uint32_t>(blue);
}

bool operator==(
    const ProvinceColor& first,
    const ProvinceColor& second
) noexcept
{
    return first.red == second.red
        && first.green == second.green
        && first.blue == second.blue;
}

bool operator!=(
    const ProvinceColor& first,
    const ProvinceColor& second
) noexcept
{
    return !(first == second);
}

}
