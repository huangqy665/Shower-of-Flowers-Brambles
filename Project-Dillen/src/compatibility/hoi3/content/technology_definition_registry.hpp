#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "technology_definition.hpp"

namespace dillen::compatibility::hoi3::content {

enum class TechnologyDeclareResult
{
    Added,
    InvalidDefinition,
    DuplicateName,
    IdCollision,
    Frozen
};

enum class TechnologyResolveResult
{
    Resolved,
    TechnologyMissing,
    AlreadyResolved,
    Frozen
};

class TechnologyDefinitionRegistry
{
public:
    TechnologyDeclareResult Declare(TechnologyDefinition definition);
    TechnologyResolveResult ResolveReferences(
        TechnologyDefinitionId id,
        std::optional<TechnologyRequirement> allow,
        std::vector<TechnologyUnitReference> activatedUnits,
        std::vector<TechnologyEffectBlock> effectBlocks
    );
    void Clear();
    void Freeze();
    bool IsFrozen() const noexcept;
    std::size_t Size() const noexcept;
    std::size_t ResolvedCount() const noexcept;
    const TechnologyDefinition* Find(TechnologyDefinitionId id) const;
    const TechnologyDefinition* Find(std::string_view name) const;
    const std::vector<TechnologyDefinition>& All() const noexcept;

private:
    void RebuildIndexes();

    std::vector<TechnologyDefinition> definitions_;
    std::unordered_map<std::uint64_t, std::size_t> indexById_;
    std::unordered_map<std::string, std::size_t> indexByName_;
    bool frozen_ = false;
};

}
