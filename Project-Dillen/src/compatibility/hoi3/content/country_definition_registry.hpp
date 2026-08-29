#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "country_tag_definition.hpp"

namespace dillen::compatibility::hoi3::content {

enum class CountryDeclareResult
{
    Added,
    InvalidDefinition,
    DuplicateTag,
    Frozen
};

enum class CountryResolveResult
{
    Resolved,
    CountryMissing,
    DefinitionMissing,
    AlreadyResolved,
    Frozen
};

class CountryDefinitionRegistry
{
public:
    CountryDeclareResult Declare(CountryTagDefinition definition);
    CountryResolveResult Resolve(
        CountryDefinitionId id,
        std::shared_ptr<const CountryDefinition> definition
    );
    void Clear();
    void Freeze();
    bool IsFrozen() const noexcept;
    std::size_t Size() const noexcept;
    std::size_t ResolvedCount() const noexcept;
    const CountryTagDefinition* Find(CountryDefinitionId id) const;
    const CountryTagDefinition* Find(const CountryTag& tag) const;
    const CountryTagDefinition* Find(std::string_view tag) const;
    const std::vector<CountryTagDefinition>& All() const noexcept;

private:
    void RebuildIndex();

    std::vector<CountryTagDefinition> definitions_;
    std::unordered_map<std::uint32_t, std::size_t> indexById_;
    bool frozen_ = false;
};

}
