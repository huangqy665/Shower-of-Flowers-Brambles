#pragma once

#include <cstdint>
#include <string>

#include "definition_origin.hpp"

namespace dillen::content {

struct ProvinceDefinitionId
{
    std::uint32_t value = 0;

    explicit operator bool() const noexcept;
};

bool operator==(
    ProvinceDefinitionId first,
    ProvinceDefinitionId second
) noexcept;
bool operator!=(
    ProvinceDefinitionId first,
    ProvinceDefinitionId second
) noexcept;
bool operator<(
    ProvinceDefinitionId first,
    ProvinceDefinitionId second
) noexcept;

struct ProvinceColor
{
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;

    std::uint32_t PackedRgb() const noexcept;
};

bool operator==(
    const ProvinceColor& first,
    const ProvinceColor& second
) noexcept;
bool operator!=(
    const ProvinceColor& first,
    const ProvinceColor& second
) noexcept;

struct ProvinceDefinition
{
    ProvinceDefinitionId id;
    ProvinceColor color;
    std::string name;
    DefinitionOrigin origin;
};

}
