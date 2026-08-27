#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "definition_origin.hpp"

namespace dillen::content {

struct CountryDefinition;

struct CountryDefinitionId
{
    std::uint32_t value = 0;

    explicit operator bool() const noexcept;
};

bool operator==(
    CountryDefinitionId first,
    CountryDefinitionId second
) noexcept;
bool operator!=(
    CountryDefinitionId first,
    CountryDefinitionId second
) noexcept;
bool operator<(
    CountryDefinitionId first,
    CountryDefinitionId second
) noexcept;

class CountryTag
{
public:
    CountryTag() = default;

    static std::optional<CountryTag> Parse(std::string_view text);

    std::string_view View() const noexcept;
    std::string ToString() const;
    CountryDefinitionId StableId() const noexcept;
    bool IsValid() const noexcept;

private:
    explicit CountryTag(std::array<char, 3> characters);

    std::array<char, 3> characters_{};
};

bool operator==(const CountryTag& first, const CountryTag& second) noexcept;
bool operator!=(const CountryTag& first, const CountryTag& second) noexcept;
bool operator<(const CountryTag& first, const CountryTag& second) noexcept;

struct CountryTagDefinition
{
    CountryDefinitionId id;
    CountryTag tag;
    std::string declaredPath;
    std::string definitionPath;
    DefinitionOrigin origin;
    std::shared_ptr<const CountryDefinition> definition;
};

}
