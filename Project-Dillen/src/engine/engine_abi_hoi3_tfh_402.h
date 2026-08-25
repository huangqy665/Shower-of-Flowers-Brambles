#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#define HOI3_TFH402_D3D9_FRAME_METHOD_OFFSET_ASM 0A8h

namespace core::engine::abi
{

inline constexpr int32_t FixedPointScale = 1000;
inline constexpr int32_t MaximumBuildingLevel = 100;
inline constexpr uint64_t ResearchCompletionThreshold = 0x8000;
inline constexpr uint32_t NativeEventLookupType = 0x28;
inline constexpr std::size_t D3d9FrameProbePatchSize = 9;
inline constexpr std::uintptr_t D3d9FrameMethodOffset = 0xA8;

struct CountryTagValue
{
    uint32_t tag = 0;
    uint32_t index = 0;
};

struct CountryEffectObject
{
    std::array<uint8_t, 0x20> padding{};
    int32_t amount = 0;
};

struct CountryEffectScope
{
    std::array<uint8_t, 0x14> padding{};
    uint32_t countryIndex = 0;
    std::array<uint8_t, 0x10> targetPadding{};
    uint32_t provinceId = 0;
};

struct IdeologyValuesEffectObject
{
    std::array<uint8_t, 0x24> padding{};
    uint32_t ideologyDefinition = 0;
    int32_t organizationAmount = 0;
    int32_t popularityAmount = 0;
};

struct ThreatEffectObject
{
    std::array<uint8_t, 0x20> padding{};
    int32_t amount = 0;
    uint32_t auxiliary = 0;
    uint32_t countryIndex = 0;
    uint8_t relativeMode = 0;
};

struct ProvinceAddEffectObject
{
    std::array<uint8_t, 0x20> padding{};
    int32_t durationDays = 0;
    uint32_t modifierDefinition = 0;
};

struct ProvinceRemoveEffectObject
{
    std::array<uint8_t, 0x20> padding{};
    uint32_t modifierDefinition = 0;
};

struct ProvinceEffectScope
{
    std::array<uint8_t, 0x28> padding{};
    uint32_t provinceId = 0;
};

struct TargetedProvinceEffectScope
{
    std::array<uint8_t, 0x10> padding{};
    CountryTagValue country{};
    std::array<uint8_t, 0x10> targetPadding{};
    uint32_t provinceId = 0;
};

struct ProvinceCountryEffectObject
{
    std::array<uint8_t, 0x20> padding{};
    CountryTagValue country{};
};

struct AddCoreEffectObject
{
    std::array<uint8_t, 0x20> padding{};
    CountryTagValue country{};
    uint32_t provinceId = 0;
};

struct RemoveCoreEffectObject
{
    std::array<uint8_t, 0x24> padding{};
    CountryTagValue country{};
    uint32_t provinceId = 0;
};

struct BuildingLevelCommandObject
{
    std::array<uint8_t, 0x3C> padding{};
    uint32_t buildingDefinition = 0;
    int32_t level = 0;
    uint32_t provinceId = 0;
};

struct CountryTechnologyCommandObject
{
    std::array<uint8_t, 0x3C> padding{};
    CountryTagValue country{};
    uint32_t technologyDefinition = 0;
};

struct TechnologyLevelCommandObject
{
    std::array<uint8_t, 0x3C> padding{};
    CountryTagValue country{};
    uint32_t technologyDefinition = 0;
    int32_t level = 0;
};

struct ResearchProgressCommandObject
{
    std::array<uint8_t, 0x3C> padding{};
    CountryTagValue country{};
    uint32_t technologyDefinition = 0;
    uint64_t progress = 0;
};

struct TechnologyInvestmentEffectObject
{
    std::array<uint8_t, 0x20> padding{};
    int32_t amount = 0;
    uint32_t technologyDefinition = 0;
};

struct CapitalEffectObject
{
    std::array<uint8_t, 0x20> padding{};
    int32_t provinceIdFixed = 0;
};

struct NativeBorrowedString32
{
    uint32_t pointer = 0;
    std::array<uint8_t, 12> reserved{};
    uint32_t size = 0;
    uint32_t capacity = 16;
};

struct GlobalFlagEffectObject
{
    std::array<uint8_t, 0x20> padding{};
    NativeBorrowedString32 name;
};

struct NativeEventScopeObject
{
    std::array<uint8_t, 0x10> prefix{};
    CountryTagValue country{};
    std::array<uint8_t, 0x10> middle{};
    uint32_t provinceId = 0;
    std::array<uint8_t, 0x1C> suffix{};
};

struct NativeDecisionCommandObject
{
    std::array<uint8_t, 0x3C> padding{};
    NativeBorrowedString32 name;
    uint32_t stringPadding = 0;
    NativeEventScopeObject scope;
};

struct NativeEventLookupKey
{
    uint32_t type = NativeEventLookupType;
    int32_t id = 0;
};

static_assert(sizeof(CountryTagValue) == 0x08);
static_assert(offsetof(CountryEffectObject, amount) == 0x20);
static_assert(offsetof(CountryEffectScope, countryIndex) == 0x14);
static_assert(offsetof(CountryEffectScope, provinceId) == 0x28);
static_assert(offsetof(IdeologyValuesEffectObject, ideologyDefinition) == 0x24);
static_assert(offsetof(IdeologyValuesEffectObject, organizationAmount) == 0x28);
static_assert(offsetof(IdeologyValuesEffectObject, popularityAmount) == 0x2C);
static_assert(offsetof(ThreatEffectObject, amount) == 0x20);
static_assert(offsetof(ThreatEffectObject, countryIndex) == 0x28);
static_assert(offsetof(ThreatEffectObject, relativeMode) == 0x2C);
static_assert(offsetof(ProvinceAddEffectObject, durationDays) == 0x20);
static_assert(offsetof(ProvinceAddEffectObject, modifierDefinition) == 0x24);
static_assert(offsetof(ProvinceRemoveEffectObject, modifierDefinition) == 0x20);
static_assert(offsetof(ProvinceEffectScope, provinceId) == 0x28);
static_assert(offsetof(TargetedProvinceEffectScope, country) == 0x10);
static_assert(offsetof(TargetedProvinceEffectScope, provinceId) == 0x28);
static_assert(offsetof(ProvinceCountryEffectObject, country) == 0x20);
static_assert(offsetof(AddCoreEffectObject, country) == 0x20);
static_assert(offsetof(AddCoreEffectObject, provinceId) == 0x28);
static_assert(offsetof(RemoveCoreEffectObject, country) == 0x24);
static_assert(offsetof(RemoveCoreEffectObject, provinceId) == 0x2C);
static_assert(offsetof(BuildingLevelCommandObject, buildingDefinition) == 0x3C);
static_assert(offsetof(BuildingLevelCommandObject, level) == 0x40);
static_assert(offsetof(BuildingLevelCommandObject, provinceId) == 0x44);
static_assert(offsetof(CountryTechnologyCommandObject, country) == 0x3C);
static_assert(offsetof(CountryTechnologyCommandObject, technologyDefinition) == 0x44);
static_assert(offsetof(TechnologyLevelCommandObject, level) == 0x48);
static_assert(offsetof(ResearchProgressCommandObject, progress) == 0x48);
static_assert(offsetof(TechnologyInvestmentEffectObject, amount) == 0x20);
static_assert(offsetof(TechnologyInvestmentEffectObject, technologyDefinition) == 0x24);
static_assert(offsetof(CapitalEffectObject, provinceIdFixed) == 0x20);
static_assert(sizeof(NativeBorrowedString32) == 0x18);
static_assert(offsetof(GlobalFlagEffectObject, name) == 0x20);
static_assert(offsetof(NativeEventScopeObject, country) == 0x10);
static_assert(offsetof(NativeEventScopeObject, provinceId) == 0x28);
static_assert(sizeof(NativeEventScopeObject) == 0x48);
static_assert(offsetof(NativeDecisionCommandObject, name) == 0x3C);
static_assert(offsetof(NativeDecisionCommandObject, scope) == 0x58);
static_assert(sizeof(NativeDecisionCommandObject) == 0xA0);

}
