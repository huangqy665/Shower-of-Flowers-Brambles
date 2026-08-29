#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "country_tag_definition.hpp"
#include "definition_origin.hpp"
#include "province_definition.hpp"
#include "unit_type_definition.hpp"

namespace dillen::compatibility::hoi3::content {

struct OrderOfBattleDefinitionId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

bool operator==(
    OrderOfBattleDefinitionId first,
    OrderOfBattleDefinitionId second
) noexcept;
bool operator!=(
    OrderOfBattleDefinitionId first,
    OrderOfBattleDefinitionId second
) noexcept;
bool operator<(
    OrderOfBattleDefinitionId first,
    OrderOfBattleDefinitionId second
) noexcept;

std::string NormalizeOrderOfBattlePath(std::string_view path);
OrderOfBattleDefinitionId StableOrderOfBattleDefinitionId(
    std::string_view virtualPath
);

enum class OrderOfBattleNodeKind
{
    Theatre,
    ArmyGroup,
    Army,
    Corps,
    Division,
    Navy,
    Air,
    Regiment,
    Ship,
    Wing
};

struct OrderOfBattleNode
{
    OrderOfBattleNodeKind kind = OrderOfBattleNodeKind::Division;
    std::string name;
    std::string unitTypeName;
    std::optional<UnitTypeDefinitionId> unitType;
    std::optional<ProvinceDefinitionId> location;
    std::optional<ProvinceDefinitionId> base;
    std::optional<std::int64_t> leader;
    std::optional<CountryDefinitionId> expeditionaryOwner;
    std::optional<CountryDefinitionId> builder;
    std::optional<bool> reserve;
    std::optional<bool> pride;
    std::optional<int> historicalModel;
    std::optional<double> experience;
    std::optional<double> strength;
    std::optional<double> organisation;
    std::optional<double> digIn;
    std::vector<OrderOfBattleNode> children;
    DefinitionOrigin origin;
};

struct OrderOfBattleMilitaryAccess
{
    CountryDefinitionId country;
    bool enabled = false;
    DefinitionOrigin origin;
};

struct OrderOfBattleConstruction
{
    std::optional<CountryDefinitionId> country;
    std::optional<CountryDefinitionId> builder;
    std::string name;
    std::optional<bool> reserve;
    std::optional<double> cost;
    std::optional<double> progress;
    std::optional<double> duration;
    std::optional<double> manpower;
    std::vector<OrderOfBattleNode> components;
    DefinitionOrigin origin;
};

struct OrderOfBattleMetadata
{
    std::string key;
    std::string value;
    DefinitionOrigin origin;
};

struct OrderOfBattleDefinition
{
    OrderOfBattleDefinitionId id;
    std::string virtualPath;
    std::vector<OrderOfBattleNode> roots;
    std::vector<OrderOfBattleMilitaryAccess> militaryAccess;
    std::vector<OrderOfBattleConstruction> constructions;
    std::vector<OrderOfBattleMetadata> metadata;
    DefinitionOrigin origin;
    bool referencesResolved = false;
};

}
