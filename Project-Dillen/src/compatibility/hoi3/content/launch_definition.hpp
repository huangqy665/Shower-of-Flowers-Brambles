#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "country_tag_definition.hpp"
#include "definition_date.hpp"
#include "definition_origin.hpp"

namespace dillen::compatibility::hoi3::content {

struct BookmarkDefinitionId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

bool operator==(
    BookmarkDefinitionId first,
    BookmarkDefinitionId second
) noexcept;
bool operator!=(
    BookmarkDefinitionId first,
    BookmarkDefinitionId second
) noexcept;
bool operator<(
    BookmarkDefinitionId first,
    BookmarkDefinitionId second
) noexcept;

struct ScenarioDefinitionId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

bool operator==(
    ScenarioDefinitionId first,
    ScenarioDefinitionId second
) noexcept;
bool operator!=(
    ScenarioDefinitionId first,
    ScenarioDefinitionId second
) noexcept;
bool operator<(
    ScenarioDefinitionId first,
    ScenarioDefinitionId second
) noexcept;

std::string NormalizeBookmarkKey(std::string_view key);
std::string NormalizeScenarioKey(std::string_view key);
BookmarkDefinitionId StableBookmarkDefinitionId(
    std::string_view key
);
ScenarioDefinitionId StableScenarioDefinitionId(
    std::string_view key
);

struct BookmarkDefinition
{
    BookmarkDefinitionId id;
    std::string key;
    std::string name;
    std::string description;
    std::string icon;
    DefinitionDate date;
    std::vector<CountryDefinitionId> recommendedCountries;
    DefinitionOrigin origin;
};

struct ScenarioDefinition
{
    ScenarioDefinitionId id;
    std::string key;
    std::string name;
    std::string description;
    std::string icon;
    DefinitionDate startDate;
    DefinitionDate endDate;
    std::vector<CountryDefinitionId> selectableCountries;
    std::vector<CountryDefinitionId> additionalCountries;
    DefinitionOrigin origin;
};

}
