#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "definition_origin.hpp"
#include "unit_type_definition.hpp"

namespace dillen::content {

struct TechnologyDefinitionId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

bool operator==(
    TechnologyDefinitionId first,
    TechnologyDefinitionId second
) noexcept;
bool operator!=(
    TechnologyDefinitionId first,
    TechnologyDefinitionId second
) noexcept;
bool operator<(
    TechnologyDefinitionId first,
    TechnologyDefinitionId second
) noexcept;

std::string NormalizeTechnologyName(std::string_view name);
TechnologyDefinitionId StableTechnologyDefinitionId(
    std::string_view name
) noexcept;

using TechnologyScalarValue = std::variant<
    std::int64_t,
    double,
    bool,
    std::string
>;

struct TechnologyScalarEffect
{
    std::string name;
    TechnologyScalarValue value = std::int64_t{0};
};

struct TechnologyEffectBlock
{
    std::string name;
    std::optional<UnitTypeDefinitionId> unitType;
    std::vector<TechnologyScalarEffect> effects;
    std::vector<TechnologyEffectBlock> blocks;
};

struct TechnologyResearchBonus
{
    std::string source;
    double weight = 0.0;
};

enum class TechnologyRequirementKind
{
    All,
    Any,
    Not,
    Level,
    Predicate
};

struct TechnologyRequirement
{
    TechnologyRequirementKind kind = TechnologyRequirementKind::All;
    std::string name;
    int level = 0;
    std::optional<TechnologyScalarValue> predicateValue;
    std::optional<TechnologyDefinitionId> technology;
    std::vector<TechnologyRequirement> children;
};

struct TechnologyUnitReference
{
    std::string name;
    std::optional<UnitTypeDefinitionId> unitType;
};

struct TechnologyDefinition
{
    TechnologyDefinitionId id;
    std::string name;
    std::string normalizedName;
    double difficulty = 0.0;
    int startYear = 0;
    std::optional<int> firstOffset;
    std::optional<int> additionalOffset;
    std::optional<int> maxLevel;
    std::string folder;
    std::string onCompletion;
    std::optional<bool> change;
    std::optional<TechnologyRequirement> allow;
    std::vector<TechnologyResearchBonus> researchBonuses;
    std::vector<TechnologyUnitReference> activatedUnits;
    std::vector<std::string> activatedBuildings;
    std::vector<TechnologyScalarEffect> scalarEffects;
    std::vector<TechnologyEffectBlock> effectBlocks;
    DefinitionOrigin origin;
    bool referencesResolved = false;
};

}
