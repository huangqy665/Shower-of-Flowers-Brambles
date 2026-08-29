#include "mechanism_schema.hpp"

#include <cmath>

namespace dillen::kernel {

namespace {

bool MatchesKind(MechanismValueKind expected, MechanismValueKind actual)
{
    return expected == actual
        || (expected == MechanismValueKind::Decimal
            && actual == MechanismValueKind::Integer);
}

std::optional<double> NumericValue(const MechanismValue& value)
{
    if (const auto* integer = std::get_if<std::int64_t>(&value.data))
    {
        return static_cast<double>(*integer);
    }
    if (const auto* decimal = std::get_if<double>(&value.data))
    {
        return *decimal;
    }
    return std::nullopt;
}

std::optional<std::size_t> ValueSize(const MechanismValue& value)
{
    if (const auto* text = std::get_if<std::string>(&value.data))
    {
        return text->size();
    }
    if (const auto* list = std::get_if<MechanismValue::List>(&value.data))
    {
        return list->size();
    }
    if (const auto* object = std::get_if<MechanismValue::Object>(&value.data))
    {
        return object->size();
    }
    return std::nullopt;
}

}

bool IsValidMechanismFieldSchema(const MechanismFieldSchema& field)
{
    const bool hasNumericConstraint = field.minimumNumber
        || field.maximumNumber;
    const bool hasSizeConstraint = field.minimumSize
        || field.maximumSize;
    if (!IsValidMechanismSymbol(field.name)
        || field.name != NormalizeMechanismSymbol(field.name)
        || (hasNumericConstraint
            && field.kind != MechanismValueKind::Integer
            && field.kind != MechanismValueKind::Decimal)
        || (hasSizeConstraint
            && field.kind != MechanismValueKind::String
            && field.kind != MechanismValueKind::List
            && field.kind != MechanismValueKind::Object)
        || (field.minimumNumber
            && !std::isfinite(*field.minimumNumber))
        || (field.maximumNumber
            && !std::isfinite(*field.maximumNumber))
        || (field.minimumNumber
            && field.maximumNumber
            && *field.minimumNumber > *field.maximumNumber)
        || (field.minimumSize
            && field.maximumSize
            && *field.minimumSize > *field.maximumSize))
    {
        return false;
    }
    if (field.listElementKind
        && field.kind != MechanismValueKind::List)
    {
        return false;
    }
    if ((field.referenceKind || field.referenceType)
        && field.kind != MechanismValueKind::Reference)
    {
        return false;
    }
    if (field.referenceType && *field.referenceType == 0)
    {
        return false;
    }
    return !field.defaultValue
        || MechanismValueMatchesSchema(field, *field.defaultValue);
}

bool MechanismValueMatchesSchema(
    const MechanismFieldSchema& schema,
    const MechanismValue& value
)
{
    if (!MatchesKind(schema.kind, value.Kind()))
    {
        return false;
    }
    if (const std::optional<double> number = NumericValue(value))
    {
        if (!std::isfinite(*number)
            || (schema.minimumNumber && *number < *schema.minimumNumber)
            || (schema.maximumNumber && *number > *schema.maximumNumber))
        {
            return false;
        }
    }
    if (const std::optional<std::size_t> size = ValueSize(value))
    {
        if ((schema.minimumSize && *size < *schema.minimumSize)
            || (schema.maximumSize && *size > *schema.maximumSize))
        {
            return false;
        }
    }
    if (schema.kind == MechanismValueKind::List
        && schema.listElementKind)
    {
        const auto* list = std::get_if<MechanismValue::List>(&value.data);
        if (list == nullptr)
        {
            return false;
        }
        for (const MechanismValue& element : *list)
        {
            if (!MatchesKind(*schema.listElementKind, element.Kind()))
            {
                return false;
            }
        }
    }
    if (schema.kind == MechanismValueKind::Reference)
    {
        const auto* reference = std::get_if<MechanismReference>(&value.data);
        if (reference == nullptr
            || reference->type == 0
            || reference->value == 0
            || (schema.referenceKind
                && reference->kind != *schema.referenceKind)
            || (schema.referenceType
                && reference->type != *schema.referenceType))
        {
            return false;
        }
    }
    return true;
}

}
