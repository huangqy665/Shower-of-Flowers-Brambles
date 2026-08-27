#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string_view>
#include <utility>
#include <vector>

#include "mechanism_schema.hpp"

namespace dillen::kernel {

enum class MechanismSchemaRegisterResult
{
    Added,
    InvalidSchema,
    DuplicateVersion,
    TypeCollision,
    Frozen
};

class MechanismSchemaRegistry
{
public:
    MechanismSchemaRegisterResult Register(MechanismSchema schema);
    void Clear();
    void Freeze();
    bool IsFrozen() const noexcept;
    std::size_t Size() const noexcept;
    const MechanismSchema* Find(
        MechanismTypeId type,
        std::uint32_t version
    ) const;
    const MechanismSchema* Find(
        std::string_view canonicalName,
        std::uint32_t version
    ) const;
    const MechanismSchema* Latest(MechanismTypeId type) const;
    const std::vector<MechanismSchema>& All() const noexcept;

private:
    void RebuildIndexes();

    std::vector<MechanismSchema> schemas_;
    std::map<std::pair<std::uint64_t, std::uint32_t>, std::size_t>
        indexByKey_;
    std::map<std::uint64_t, std::size_t> latestByType_;
    bool frozen_ = false;
};

}
