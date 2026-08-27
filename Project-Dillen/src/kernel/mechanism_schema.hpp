#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "mechanism_ids.hpp"
#include "mechanism_value.hpp"

namespace dillen::kernel {

struct MechanismFieldSchema
{
    std::string name;
    MechanismValueKind kind = MechanismValueKind::Null;
    bool required = false;
    std::optional<MechanismValue> defaultValue;
    std::optional<double> minimumNumber;
    std::optional<double> maximumNumber;
    std::optional<std::size_t> minimumSize;
    std::optional<std::size_t> maximumSize;
    std::optional<MechanismValueKind> listElementKind;
    std::optional<MechanismReferenceKind> referenceKind;
    std::optional<std::uint64_t> referenceType;
};

struct MechanismRoleSchema
{
    std::string name;
    MechanismReferenceKind referenceKind =
        MechanismReferenceKind::Entity;
    std::optional<std::uint64_t> referenceType;
    std::size_t minimumCount = 0;
    std::optional<std::size_t> maximumCount = 1;
};

struct MechanismSchema
{
    MechanismTypeId type;
    std::string canonicalName;
    std::uint32_t version = 0;
    std::vector<MechanismFieldSchema> fields;
    std::vector<MechanismRoleSchema> roles;
};

bool MechanismValueMatchesSchema(
    const MechanismFieldSchema& schema,
    const MechanismValue& value
);

}
