#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mechanism_ids.hpp"
#include "mechanism_schema.hpp"

namespace dillen::kernel {

struct ComponentSchema
{
    ComponentTypeId type;
    std::string canonicalName;
    std::uint32_t version = 0;
    std::vector<MechanismFieldSchema> fields;
};

enum class ComponentSchemaRegisterResult
{
    Added,
    InvalidSchema,
    DuplicateVersion,
    TypeCollision,
    Frozen
};

class ComponentSchemaRegistry
{
public:
    ComponentSchemaRegisterResult Register(ComponentSchema schema);
    void Clear();
    void Freeze();
    bool IsFrozen() const noexcept;
    std::size_t Size() const noexcept;
    const ComponentSchema* Find(
        ComponentTypeId type,
        std::uint32_t version
    ) const;
    const ComponentSchema* Find(
        std::string_view canonicalName,
        std::uint32_t version
    ) const;
    const std::vector<ComponentSchema>& All() const noexcept;

private:
    void RebuildIndex();

    std::vector<ComponentSchema> schemas_;
    std::map<std::pair<std::uint64_t, std::uint32_t>, std::size_t>
        indexByVersion_;
    bool frozen_ = false;
};

}
