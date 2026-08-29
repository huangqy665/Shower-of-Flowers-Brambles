#include "country_tag_definition.hpp"

#include <algorithm>
#include <cctype>

namespace dillen::compatibility::hoi3::content {

namespace {

bool IsTagCharacter(char character)
{
    return (character >= 'A' && character <= 'Z')
        || (character >= 'a' && character <= 'z')
        || (character >= '0' && character <= '9')
        || character == '_'
        || character == '-';
}

}

CountryDefinitionId::operator bool() const noexcept
{
    return value != 0;
}

bool operator==(
    CountryDefinitionId first,
    CountryDefinitionId second
) noexcept
{
    return first.value == second.value;
}

bool operator!=(
    CountryDefinitionId first,
    CountryDefinitionId second
) noexcept
{
    return !(first == second);
}

bool operator<(
    CountryDefinitionId first,
    CountryDefinitionId second
) noexcept
{
    return first.value < second.value;
}

CountryTag::CountryTag(std::array<char, 3> characters)
    : characters_(characters)
{
}

std::optional<CountryTag> CountryTag::Parse(std::string_view text)
{
    if (text.size() != 3
        || !std::all_of(
            text.begin(),
            text.end(),
            IsTagCharacter))
    {
        return std::nullopt;
    }
    std::array<char, 3> characters{};
    std::transform(
        text.begin(),
        text.end(),
        characters.begin(),
        [](char character)
        {
            return static_cast<char>(std::toupper(
                static_cast<unsigned char>(character)
            ));
        }
    );
    return CountryTag(characters);
}

std::string_view CountryTag::View() const noexcept
{
    return std::string_view(characters_.data(), characters_.size());
}

std::string CountryTag::ToString() const
{
    return std::string(characters_.data(), characters_.size());
}

CountryDefinitionId CountryTag::StableId() const noexcept
{
    return {
        (static_cast<std::uint32_t>(
            static_cast<unsigned char>(characters_[0])) << 16)
        | (static_cast<std::uint32_t>(
            static_cast<unsigned char>(characters_[1])) << 8)
        | static_cast<std::uint32_t>(
            static_cast<unsigned char>(characters_[2]))
    };
}

bool CountryTag::IsValid() const noexcept
{
    return static_cast<bool>(StableId());
}

bool operator==(
    const CountryTag& first,
    const CountryTag& second
) noexcept
{
    return first.View() == second.View();
}

bool operator!=(
    const CountryTag& first,
    const CountryTag& second
) noexcept
{
    return !(first == second);
}

bool operator<(
    const CountryTag& first,
    const CountryTag& second
) noexcept
{
    return first.View() < second.View();
}

}
