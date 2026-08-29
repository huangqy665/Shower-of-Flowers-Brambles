#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mechanism_ids.hpp"

namespace dillen::kernel {

struct RelationSchema
{
    RelationTypeId type;
    std::string canonicalName;
    std::uint32_t version = 0;
    std::optional<EntityTypeId> sourceType;
    std::optional<EntityTypeId> targetType;
    bool allowSelf = false;
};

enum class RelationSchemaRegisterResult
{
    Added,
    InvalidSchema,
    DuplicateVersion,
    TypeCollision,
    Frozen
};

class RelationSchemaRegistry
{
public:
    RelationSchemaRegisterResult Register(RelationSchema schema);
    void Clear();
    void Freeze();
    bool IsFrozen() const noexcept;
    std::size_t Size() const noexcept;
    const RelationSchema* Find(
        RelationTypeId type,
        std::uint32_t version
    ) const;
    const RelationSchema* Find(
        std::string_view canonicalName,
        std::uint32_t version
    ) const;
    const std::vector<RelationSchema>& All() const noexcept;

private:
    void RebuildIndex();

    std::vector<RelationSchema> schemas_;
    std::map<std::pair<std::uint64_t, std::uint32_t>, std::size_t>
        indexByVersion_;
    bool frozen_ = false;
};

}
