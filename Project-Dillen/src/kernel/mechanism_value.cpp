#include "mechanism_value.hpp"

#include <utility>

namespace dillen::kernel {

bool operator==(
    const MechanismReference& first,
    const MechanismReference& second
) noexcept
{
    return first.kind == second.kind
        && first.type == second.type
        && first.value == second.value;
}

bool operator!=(
    const MechanismReference& first,
    const MechanismReference& second
) noexcept
{
    return !(first == second);
}

MechanismValue::MechanismValue(bool value)
    : data(value)
{
}

MechanismValue::MechanismValue(std::int64_t value)
    : data(value)
{
}

MechanismValue::MechanismValue(double value)
    : data(value)
{
}

MechanismValue::MechanismValue(std::string value)
    : data(std::move(value))
{
}

MechanismValue::MechanismValue(const char* value)
    : data(std::string(value == nullptr ? "" : value))
{
}

MechanismValue::MechanismValue(MechanismReference value)
    : data(value)
{
}

MechanismValue::MechanismValue(List value)
    : data(std::move(value))
{
}

MechanismValue::MechanismValue(Object value)
    : data(std::move(value))
{
}

MechanismValueKind MechanismValue::Kind() const noexcept
{
    return static_cast<MechanismValueKind>(data.index());
}

bool MechanismValue::IsScalar() const noexcept
{
    return Kind() != MechanismValueKind::List
        && Kind() != MechanismValueKind::Object;
}

bool operator==(
    const MechanismValue& first,
    const MechanismValue& second
)
{
    return first.data == second.data;
}

bool operator!=(
    const MechanismValue& first,
    const MechanismValue& second
)
{
    return !(first == second);
}

}
