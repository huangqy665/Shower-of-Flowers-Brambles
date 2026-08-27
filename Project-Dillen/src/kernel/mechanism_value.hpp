#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <variant>
#include <vector>

namespace dillen::kernel {

enum class MechanismReferenceKind
{
    Entity,
    MechanismDefinition,
    MechanismInstance,
    Resource,
    Custom
};

struct MechanismReference
{
    MechanismReferenceKind kind = MechanismReferenceKind::Entity;
    std::uint64_t type = 0;
    std::uint64_t value = 0;
};

bool operator==(
    const MechanismReference& first,
    const MechanismReference& second
) noexcept;
bool operator!=(
    const MechanismReference& first,
    const MechanismReference& second
) noexcept;

enum class MechanismValueKind
{
    Null,
    Boolean,
    Integer,
    Decimal,
    String,
    Reference,
    List,
    Object
};

struct MechanismValue
{
    using List = std::vector<MechanismValue>;
    using Object = std::map<std::string, MechanismValue>;
    using Storage = std::variant<
        std::monostate,
        bool,
        std::int64_t,
        double,
        std::string,
        MechanismReference,
        List,
        Object
    >;

    MechanismValue() = default;
    explicit MechanismValue(bool value);
    explicit MechanismValue(std::int64_t value);
    explicit MechanismValue(double value);
    explicit MechanismValue(std::string value);
    explicit MechanismValue(const char* value);
    explicit MechanismValue(MechanismReference value);
    explicit MechanismValue(List value);
    explicit MechanismValue(Object value);

    MechanismValueKind Kind() const noexcept;
    bool IsScalar() const noexcept;

    Storage data;
};

bool operator==(
    const MechanismValue& first,
    const MechanismValue& second
);
bool operator!=(
    const MechanismValue& first,
    const MechanismValue& second
);

}
