#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace dillen::kernel {

struct MechanismTypeId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

struct MechanismDefinitionId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

struct MechanismInstanceId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

struct AlgorithmId
{
    std::uint64_t value = 0;

    explicit operator bool() const noexcept;
};

bool operator==(MechanismTypeId first, MechanismTypeId second) noexcept;
bool operator!=(MechanismTypeId first, MechanismTypeId second) noexcept;
bool operator<(MechanismTypeId first, MechanismTypeId second) noexcept;
bool operator==(
    MechanismDefinitionId first,
    MechanismDefinitionId second
) noexcept;
bool operator!=(
    MechanismDefinitionId first,
    MechanismDefinitionId second
) noexcept;
bool operator<(
    MechanismDefinitionId first,
    MechanismDefinitionId second
) noexcept;
bool operator==(
    MechanismInstanceId first,
    MechanismInstanceId second
) noexcept;
bool operator!=(
    MechanismInstanceId first,
    MechanismInstanceId second
) noexcept;
bool operator<(
    MechanismInstanceId first,
    MechanismInstanceId second
) noexcept;
bool operator==(AlgorithmId first, AlgorithmId second) noexcept;
bool operator!=(AlgorithmId first, AlgorithmId second) noexcept;
bool operator<(AlgorithmId first, AlgorithmId second) noexcept;

std::string NormalizeMechanismSymbol(std::string_view symbol);
bool IsValidMechanismSymbol(std::string_view symbol) noexcept;
MechanismTypeId StableMechanismTypeId(std::string_view canonicalName);
MechanismDefinitionId StableMechanismDefinitionId(
    MechanismTypeId type,
    std::string_view canonicalName
);
MechanismInstanceId StableMechanismInstanceId(
    MechanismDefinitionId definition,
    std::uint64_t creationOrdinal
);
AlgorithmId StableAlgorithmId(std::string_view canonicalName);

}
