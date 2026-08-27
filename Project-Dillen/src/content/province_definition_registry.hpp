#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "province_definition.hpp"

namespace dillen::content {

enum class ProvinceDeclareResult
{
    Added,
    InvalidDefinition,
    DuplicateId,
    DuplicateColor,
    Frozen
};

class ProvinceDefinitionRegistry
{
public:
    ProvinceDeclareResult Declare(ProvinceDefinition definition);
    void Clear();
    void Freeze();
    bool IsFrozen() const noexcept;
    std::size_t Size() const noexcept;
    const ProvinceDefinition* Find(ProvinceDefinitionId id) const;
    const ProvinceDefinition* Find(std::uint32_t id) const;
    const ProvinceDefinition* FindByColor(ProvinceColor color) const;
    const ProvinceDefinition* FindByPackedRgb(
        std::uint32_t packedRgb
    ) const;
    const std::vector<ProvinceDefinition>& All() const noexcept;

private:
    void RebuildIndexes();

    std::vector<ProvinceDefinition> definitions_;
    std::unordered_map<std::uint32_t, std::size_t> indexById_;
    std::unordered_map<std::uint32_t, std::size_t> indexByColor_;
    bool frozen_ = false;
};

}
