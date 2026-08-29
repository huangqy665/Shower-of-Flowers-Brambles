#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "country_tag_definition.hpp"
#include "definition_origin.hpp"

namespace dillen::compatibility::hoi3::content {

struct UnitTypeDefinitionId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

bool operator==(
    UnitTypeDefinitionId first,
    UnitTypeDefinitionId second
) noexcept;
bool operator!=(
    UnitTypeDefinitionId first,
    UnitTypeDefinitionId second
) noexcept;
bool operator<(
    UnitTypeDefinitionId first,
    UnitTypeDefinitionId second
) noexcept;

std::string NormalizeUnitTypeName(std::string_view name);
UnitTypeDefinitionId StableUnitTypeDefinitionId(
    std::string_view name
) noexcept;

enum class UnitDomain
{
    Land,
    Naval,
    Air
};

using UnitScalarValue = std::variant<
    std::int64_t,
    double,
    bool,
    std::string
>;

struct UnitScalarProperty
{
    std::string name;
    UnitScalarValue value = std::int64_t{0};
};

struct UnitNumericModifier
{
    std::string name;
    double value = 0.0;
};

struct UnitModifierBlock
{
    std::string name;
    std::vector<UnitNumericModifier> modifiers;
};

struct UnitTypeDefinition
{
    UnitTypeDefinitionId id;
    std::string name;
    std::string normalizedName;
    UnitDomain domain = UnitDomain::Land;
    std::optional<std::string> sprite;
    std::optional<bool> active;
    std::optional<std::string> unitGroup;
    std::vector<CountryDefinitionId> usableBy;
    std::vector<UnitScalarProperty> scalarProperties;
    std::vector<UnitModifierBlock> modifierBlocks;
    DefinitionOrigin origin;
    bool usableByResolved = false;
};

}
