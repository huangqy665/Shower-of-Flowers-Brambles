#include "hoi3_gameplay_effects.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "engine_abi_hoi3_tfh_402.h"
#include "engine_registry.h"
#include "hoi3_native_object_keys.h"
#include "native_effect_bridge.h"
#include "native_object_resolver.h"

namespace core
{
namespace
{

constexpr const auto& Profile = engine::Symbols;
using namespace engine::field;
using namespace engine::abi;
constexpr std::size_t MaximumCountryCount = 4096;
constexpr std::size_t MaximumProvinceCount = 65536;
constexpr std::size_t MaximumModifierNodes = 65536;
constexpr std::size_t MaximumOwnedProvinceNodes = 65536;
constexpr std::size_t MaximumCoreNodes = 4096;
constexpr std::size_t MaximumResearchNodes = 4096;
constexpr int32_t MaximumPercentage = 100 * FixedPointScale;
constexpr int64_t MaximumModifierDurationDays = 365000;
constexpr int64_t MaximumDispatchDelayDays = 365000;
constexpr std::size_t MaximumQueuedDispatches = 1024;
constexpr std::size_t MaximumScriptIdentifierLength = 255;

enum class CountryScalarKind
{
    Manpower,
    Dissent,
    Neutrality,
    Officers
};

enum class DirectCountryScalarKind
{
    Convoys,
    Escorts,
    FreeSpies
};

enum class IdeologyValueKind
{
    Popularity,
    Organization
};

enum class NamedCountryDefinitionKind
{
    Government,
    RulingIdeology
};

struct CountryScalarSpec
{
    const char* name = nullptr;
    std::uintptr_t fieldOffset = 0;
    std::uintptr_t executeRva = 0;
    int32_t minimum = 0;
    int32_t maximum = 0;
};

struct GoodsSpec
{
    const char* name = nullptr;
    std::uintptr_t fieldOffset = 0;
    std::uintptr_t executeRva = 0;
};

struct DirectCountryScalarSpec
{
    const char* name = nullptr;
    std::uintptr_t fieldOffset = 0;
    int32_t scale = 1;
    int32_t minimum = 0;
    int32_t maximum = 0;
    bool integerOnly = false;
};

template <typename T>
bool TryRead(std::uintptr_t address, T& output) noexcept
{
    if (!address)
    {
        return false;
    }
#if defined(_MSC_VER)
    __try
    {
        std::memcpy(&output, reinterpret_cast<const void*>(address), sizeof(T));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        output = T{};
        return false;
    }
#else
    std::memcpy(&output, reinterpret_cast<const void*>(address), sizeof(T));
    return true;
#endif
}

template <typename T>
bool TryWrite(std::uintptr_t address, const T& value) noexcept
{
    if (!address)
    {
        return false;
    }
#if defined(_MSC_VER)
    __try
    {
        std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(T));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(T));
    return true;
#endif
}

std::string UpperTag(std::string value)
{
    value.erase(
        std::remove_if(
            value.begin(),
            value.end(),
            [](unsigned char character)
            {
                return std::isspace(character) != 0;
            }
        ),
        value.end()
    );
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(std::toupper(character));
        }
    );
    return value;
}

const NativeEffectValue* FindFirst(
    const NativeEffect& effect,
    std::initializer_list<std::string_view> names
)
{
    for (const std::string_view name : names)
    {
        if (const NativeEffectValue* value = effect.Find(name))
        {
            return value;
        }
    }
    return nullptr;
}

bool ReadRequiredInteger(
    const NativeEffect& effect,
    std::initializer_list<std::string_view> names,
    int64_t& output,
    std::string_view errorCode,
    std::string& error
)
{
    const NativeEffectValue* value = FindFirst(effect, names);
    if (!value || !NativeEffectValueToInteger(*value, output))
    {
        error.assign(errorCode);
        return false;
    }
    return true;
}

bool ReadRequiredNumber(
    const NativeEffect& effect,
    std::initializer_list<std::string_view> names,
    double& output,
    std::string_view errorCode,
    std::string& error
)
{
    const NativeEffectValue* value = FindFirst(effect, names);
    if (!value
        || !NativeEffectValueToNumber(*value, output)
        || !std::isfinite(output))
    {
        error.assign(errorCode);
        return false;
    }
    return true;
}

bool ReadRequiredString(
    const NativeEffect& effect,
    std::initializer_list<std::string_view> names,
    std::string& output,
    std::string_view errorCode,
    std::string& error
)
{
    const NativeEffectValue* value = FindFirst(effect, names);
    if (!value
        || !NativeEffectValueToString(*value, output)
        || output.empty())
    {
        error.assign(errorCode);
        return false;
    }
    return true;
}

bool ResolveGameState(
    const std::uint8_t* base,
    std::uintptr_t& gameState
) noexcept
{
    gameState = 0;
    return base
        && TryRead(
            reinterpret_cast<std::uintptr_t>(base)
                + Profile.gameStateSingleton,
            gameState
        )
        && gameState != 0;
}

bool ReadCountryTag(
    std::uintptr_t country,
    std::string& tag
) noexcept
{
    std::array<char, 4> bytes{};
    if (!TryRead(country + CountryTagOffset, bytes)
        || bytes[3] != 0)
    {
        return false;
    }
    tag.assign(bytes.data(), 3);
    return true;
}

bool ReadCountryTagValue(
    std::uintptr_t country,
    CountryTagValue& value
) noexcept
{
    return TryRead(country + CountryTagOffset, value)
        && value.index < MaximumCountryCount;
}

bool ResolveNativeObject(
    const NativeObjectKey& key,
    const std::shared_ptr<void>& safetyLease,
    engine::ObjectHandle& handle,
    std::string& error
) noexcept
{
    NativeObjectResolverService& resolvers =
        GetNativeObjectResolverService();
    return safetyLease
        ? resolvers.ResolveGuarded(key, safetyLease, handle, error)
        : resolvers.Resolve(key, handle, error);
}

bool ResolveCountryByTag(
    const std::uint8_t* base,
    std::string requestedTag,
    std::uintptr_t& country,
    CountryTagValue& nativeTag,
    std::string& error,
    const std::shared_ptr<void>& safetyLease = {}
) noexcept
{
    requestedTag = UpperTag(std::move(requestedTag));
    country = 0;
    nativeTag = {};
    uint64_t stableId = 0;
    engine::ObjectHandle handle;
    if (!base || !PackHoi3CountryTag(requestedTag, stableId))
    {
        error = "country_tag_invalid";
        return false;
    }
    if (!ResolveNativeObject(
            {engine::TypeId::Country, stableId},
            safetyLease,
            handle,
            error
        )
        || !ReadCountryTagValue(handle.address, nativeTag))
    {
        if (error.empty())
        {
            error = "hoi3_country_not_found: " + requestedTag;
        }
        country = 0;
        nativeTag = {};
        return false;
    }
    country = handle.address;
    std::string resolvedTag;
    if (!ReadCountryTag(country, resolvedTag)
        || UpperTag(resolvedTag) != requestedTag)
    {
        error = "hoi3_country_tag_mismatch: " + requestedTag;
        country = 0;
        nativeTag = {};
        return false;
    }
    return true;
}

bool ResolveRequiredCountryByTag(
    const std::uint8_t* base,
    const NativeEffect& effect,
    std::initializer_list<std::string_view> names,
    std::uintptr_t& country,
    CountryTagValue& nativeTag,
    std::string_view errorCode,
    std::string& error,
    const std::shared_ptr<void>& safetyLease = {}
) noexcept
{
    std::string requestedTag;
    if (!ReadRequiredString(
            effect,
            names,
            requestedTag,
            errorCode,
            error
        ))
    {
        return false;
    }
    return ResolveCountryByTag(
        base,
        std::move(requestedTag),
        country,
        nativeTag,
        error,
        safetyLease
    );
}

bool ResolvePlayerCountryTarget(
    const std::uint8_t* base,
    const NativeEffect& effect,
    const NativeEffectExecutionContext& context,
    std::uintptr_t& gameState,
    std::uintptr_t& country,
    uint32_t& countryIndex,
    std::string& error
) noexcept
{
    std::string requestedTag = context.playerTag;
    if (const NativeEffectValue* tagValue = FindFirst(
            effect,
            {"tag", "country_tag", "countrytag"}
        ))
    {
        if (!NativeEffectValueToString(*tagValue, requestedTag))
        {
            error = "country_tag_invalid";
            return false;
        }
    }
    requestedTag = UpperTag(std::move(requestedTag));
    if (requestedTag.size() != 3
        || requestedTag != UpperTag(context.playerTag))
    {
        error = "country_target_not_player";
        return false;
    }

    gameState = 0;
    country = 0;
    countryIndex = 0;
    CountryTagValue nativeIdentity{};
    uint32_t playerCountryIndex = 0;
    if (!ResolveGameState(base, gameState))
    {
        error = "hoi3_game_state_unavailable";
        return false;
    }
    if (!ResolveCountryByTag(
            base,
            requestedTag,
            country,
            nativeIdentity,
            error,
            context.safetyLease
        )
        || !TryRead(
            gameState + PlayerCountryIndexOffset,
            playerCountryIndex
        )
        || playerCountryIndex != nativeIdentity.index)
    {
        error = "hoi3_player_country_mismatch";
        return false;
    }
    countryIndex = nativeIdentity.index;

    std::string nativeTag;
    if (!ReadCountryTag(country, nativeTag)
        || UpperTag(nativeTag) != requestedTag)
    {
        error = "hoi3_player_country_mismatch";
        return false;
    }
    return true;
}

bool ScaleFixedPoint(
    double value,
    int32_t& scaledValue,
    std::string_view errorPrefix,
    std::string& error
)
{
    const double scaled = value * FixedPointScale;
    if (!std::isfinite(value)
        || scaled < std::numeric_limits<int32_t>::min()
        || scaled > std::numeric_limits<int32_t>::max())
    {
        error.assign(errorPrefix);
        error += "_out_of_range";
        return false;
    }
    scaledValue = static_cast<int32_t>(std::llround(scaled));
    return true;
}

CountryScalarSpec GetCountryScalarSpec(CountryScalarKind kind)
{
    switch (kind)
    {
    case CountryScalarKind::Manpower:
        return {
            "manpower",
            CountryManpowerOffset,
            Profile.manpowerEffectExecute,
            0,
            std::numeric_limits<int32_t>::max()
        };
    case CountryScalarKind::Dissent:
        return {
            "dissent",
            CountryDissentOffset,
            Profile.dissentEffectExecute,
            0,
            MaximumPercentage
        };
    case CountryScalarKind::Neutrality:
        return {
            "neutrality",
            CountryNeutralityOffset,
            Profile.neutralityEffectExecute,
            0,
            MaximumPercentage
        };
    case CountryScalarKind::Officers:
        return {
            "officers",
            CountryOfficerPoolOffset,
            Profile.officerPoolEffectExecute,
            0,
            std::numeric_limits<int32_t>::max()
        };
    }
    return {};
}

DirectCountryScalarSpec GetDirectCountryScalarSpec(
    DirectCountryScalarKind kind
)
{
    switch (kind)
    {
    case DirectCountryScalarKind::Convoys:
        return {
            "convoys",
            CountryConvoyPoolOffset,
            1,
            0,
            std::numeric_limits<int32_t>::max(),
            true
        };
    case DirectCountryScalarKind::Escorts:
        return {
            "escorts",
            CountryEscortPoolOffset,
            1,
            0,
            std::numeric_limits<int32_t>::max(),
            true
        };
    case DirectCountryScalarKind::FreeSpies:
        return {
            "free_spies",
            CountryFreeSpyPoolOffset,
            FixedPointScale,
            0,
            std::numeric_limits<int32_t>::max(),
            false
        };
    }
    return {};
}

bool ResolveGoodsSpec(std::string name, GoodsSpec& spec)
{
    name = NormalizeNativeEffectName(name);
    std::replace(name.begin(), name.end(), '-', '_');
    std::replace(name.begin(), name.end(), ' ', '_');
    if (name == "supplies" || name == "supply")
    {
        spec = {
            "supplies",
            GoodsPoolSuppliesOffset,
            Profile.suppliesEffectExecute
        };
    }
    else if (name == "fuel")
    {
        spec = {"fuel", GoodsPoolFuelOffset, Profile.fuelEffectExecute};
    }
    else if (name == "money" || name == "cash")
    {
        spec = {"money", GoodsPoolMoneyOffset, Profile.moneyEffectExecute};
    }
    else if (name == "crude_oil" || name == "oil")
    {
        spec = {
            "crude_oil",
            GoodsPoolCrudeOilOffset,
            Profile.crudeOilEffectExecute
        };
    }
    else if (name == "metal")
    {
        spec = {"metal", GoodsPoolMetalOffset, Profile.metalEffectExecute};
    }
    else if (name == "energy")
    {
        spec = {"energy", GoodsPoolEnergyOffset, Profile.energyEffectExecute};
    }
    else if (name == "rare_materials"
        || name == "rare_material"
        || name == "rares"
        || name == "rare")
    {
        spec = {
            "rare_materials",
            GoodsPoolRareMaterialsOffset,
            Profile.rareMaterialsEffectExecute
        };
    }
    else
    {
        spec = {};
        return false;
    }
    return true;
}

bool ResolveProvince(
    std::uintptr_t gameState,
    int64_t provinceId,
    std::uintptr_t& province,
    std::string& error,
    const std::shared_ptr<void>& safetyLease = {}
) noexcept
{
    province = 0;
    engine::ObjectHandle handle;
    if (!gameState
        || provinceId <= 0
        || provinceId >= static_cast<int64_t>(MaximumProvinceCount))
    {
        error = "hoi3_province_id_invalid";
        return false;
    }
    if (!ResolveNativeObject(
            {
                engine::TypeId::Province,
                static_cast<uint64_t>(provinceId)
            },
            safetyLease,
            handle,
            error
        ))
    {
        return false;
    }
    province = handle.address;
    return true;
}

struct Hoi3StringStorage
{
    char data[16]{};
    uint32_t size = 0;
    uint32_t capacity = 15;
};

static_assert(sizeof(Hoi3StringStorage) == 24);

bool InvokeStringConstructor(
    std::uintptr_t address,
    Hoi3StringStorage* storage,
    const char* value,
    uint32_t length
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    using Function = void* (__thiscall*)(void*, const char*, uint32_t);
    __try
    {
        reinterpret_cast<Function>(address)(storage, value, length);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    (void)address;
    (void)storage;
    (void)value;
    (void)length;
    return false;
#endif
}

void InvokeStringDestructor(
    std::uintptr_t address,
    Hoi3StringStorage* storage
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    using Function = void (__thiscall*)(void*);
    __try
    {
        reinterpret_cast<Function>(address)(storage);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
    }
#else
    (void)address;
    (void)storage;
#endif
}

void* InvokeModifierLookup(
    std::uintptr_t address,
    void* registry,
    Hoi3StringStorage* name
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    void* result = nullptr;
    __try
    {
        __asm
        {
            mov eax, registry
            push name
            mov edx, address
            call edx
            mov result, eax
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        result = nullptr;
    }
    return result;
#else
    (void)address;
    (void)registry;
    (void)name;
    return nullptr;
#endif
}

std::string NormalizeDefinitionKey(std::string value)
{
    value.erase(
        std::remove_if(
            value.begin(),
            value.end(),
            [](unsigned char character)
            {
                return std::isspace(character) != 0;
            }
        ),
        value.end()
    );
    std::transform(
        value.begin(),
        value.end(),
        value.begin(),
        [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        }
    );
    std::replace(value.begin(), value.end(), '-', '_');
    return value;
}

void* InvokeNamedDefinitionLookup(
    std::uintptr_t address,
    void* registry,
    Hoi3StringStorage* name
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    void* result = nullptr;
    __try
    {
        __asm
        {
            mov eax, registry
            push name
            mov edx, address
            call edx
            mov result, eax
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        result = nullptr;
    }
    return result;
#else
    (void)address;
    (void)registry;
    (void)name;
    return nullptr;
#endif
}

void* FindNamedDefinition(
    const std::uint8_t* base,
    std::string name,
    NamedCountryDefinitionKind kind,
    std::string& error
) noexcept
{
    name = NormalizeDefinitionKey(std::move(name));
    const std::uintptr_t registryRva =
        kind == NamedCountryDefinitionKind::Government
        ? Profile.governmentRegistrySingleton
        : Profile.ideologyRegistrySingleton;
    const std::uintptr_t lookupRva =
        kind == NamedCountryDefinitionKind::Government
        ? Profile.governmentLookup
        : Profile.ideologyLookup;
    const char* const prefix =
        kind == NamedCountryDefinitionKind::Government
        ? "government"
        : "ideology";
    if (name.empty())
    {
        error = std::string(prefix) + "_name_invalid";
        return nullptr;
    }

    std::uintptr_t registry = 0;
    if (!TryRead(
            reinterpret_cast<std::uintptr_t>(base) + registryRva,
            registry
        )
        || !registry)
    {
        error = std::string("hoi3_") + prefix + "_registry_unavailable";
        return nullptr;
    }

    Hoi3StringStorage storage{};
    if (!InvokeStringConstructor(
            reinterpret_cast<std::uintptr_t>(base)
                + Profile.stringConstructor,
            &storage,
            name.data(),
            static_cast<uint32_t>(name.size())
        ))
    {
        error = std::string("hoi3_") + prefix
            + "_name_construction_failed";
        return nullptr;
    }
    void* definition = InvokeNamedDefinitionLookup(
        reinterpret_cast<std::uintptr_t>(base) + lookupRva,
        reinterpret_cast<void*>(registry),
        &storage
    );
    InvokeStringDestructor(
        reinterpret_cast<std::uintptr_t>(base) + Profile.stringDestructor,
        &storage
    );
    if (!definition)
    {
        error = std::string(prefix) + "_definition_not_found: " + name;
    }
    return definition;
}

void* FindModifierDefinition(
    const std::uint8_t* base,
    const std::string& name,
    std::string& error
) noexcept
{
    if (name.empty()
        || name.size() > static_cast<std::size_t>(
            std::numeric_limits<uint32_t>::max()
        ))
    {
        error = "modifier_name_invalid";
        return nullptr;
    }

    std::uintptr_t registry = 0;
    if (!TryRead(
            reinterpret_cast<std::uintptr_t>(base)
                + Profile.modifierRegistrySingleton,
            registry
        )
        || !registry)
    {
        error = "hoi3_modifier_registry_unavailable";
        return nullptr;
    }

    Hoi3StringStorage storage{};
    if (!InvokeStringConstructor(
            reinterpret_cast<std::uintptr_t>(base)
                + Profile.stringConstructor,
            &storage,
            name.data(),
            static_cast<uint32_t>(name.size())
        ))
    {
        error = "hoi3_modifier_name_construction_failed";
        return nullptr;
    }
    void* definition = InvokeModifierLookup(
        reinterpret_cast<std::uintptr_t>(base) + Profile.modifierLookup,
        reinterpret_cast<void*>(registry),
        &storage
    );
    InvokeStringDestructor(
        reinterpret_cast<std::uintptr_t>(base) + Profile.stringDestructor,
        &storage
    );
    if (!definition)
    {
        error = "modifier_definition_not_found: " + name;
    }
    return definition;
}

void* InvokeCStringDefinitionLookup(
    const std::uint8_t* base,
    std::uintptr_t lookupRva,
    const char* name
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    using Function = void* (__cdecl*)(const char*);
    void* result = nullptr;
    __try
    {
        result = reinterpret_cast<Function>(
            reinterpret_cast<std::uintptr_t>(base) + lookupRva
        )(name);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        result = nullptr;
    }
    return result;
#else
    (void)base;
    (void)lookupRva;
    (void)name;
    return nullptr;
#endif
}

void* FindCStringDefinition(
    const std::uint8_t* base,
    std::string name,
    std::uintptr_t lookupRva,
    std::string_view kind,
    std::string& error
) noexcept
{
    name = NormalizeDefinitionKey(std::move(name));
    if (name.empty())
    {
        error = std::string(kind) + "_name_invalid";
        return nullptr;
    }
    void* const definition = InvokeCStringDefinitionLookup(
        base,
        lookupRva,
        name.c_str()
    );
    if (!definition)
    {
        error = std::string(kind) + "_definition_not_found: " + name;
    }
    return definition;
}

void* FindBuildingDefinition(
    const std::uint8_t* base,
    std::string name,
    std::string& error
) noexcept
{
    return FindCStringDefinition(
        base,
        std::move(name),
        Profile.buildingLookup,
        "building",
        error
    );
}

void* FindTechnologyDefinition(
    const std::uint8_t* base,
    std::string name,
    std::string& error,
    const std::shared_ptr<void>& safetyLease = {}
) noexcept
{
    if (!base)
    {
        error = "hoi3_technology_registry_unavailable";
        return nullptr;
    }
    name = NormalizeHoi3DefinitionName(name);
    engine::ObjectHandle handle;
    if (name.empty()
        || !ResolveNativeObject(
            {engine::TypeId::TechnologyDefinition, 0, name},
            safetyLease,
            handle,
            error
        ))
    {
        if (error.empty())
        {
            error = "technology_definition_not_found: " + name;
        }
        return nullptr;
    }
    return reinterpret_cast<void*>(handle.address);
}

struct ModifierInstance
{
    bool found = false;
    std::uintptr_t record = 0;
    int32_t expiry = 0;
};

bool FindModifierInstance(
    std::uintptr_t object,
    std::uintptr_t listOffset,
    std::uintptr_t definition,
    ModifierInstance& instance
) noexcept
{
    instance = {};
    std::uintptr_t node = 0;
    if (!TryRead(object + listOffset, node))
    {
        return false;
    }
    for (std::size_t guard = 0;
         node && guard < MaximumModifierNodes;
         ++guard)
    {
        std::uintptr_t record = 0;
        std::uintptr_t next = 0;
        std::uintptr_t currentDefinition = 0;
        int32_t expiry = 0;
        if (!TryRead(node + ModifierListNodeRecordOffset, record)
            || !TryRead(node + ModifierListNodeNextOffset, next)
            || !record
            || !TryRead(
                record + ModifierRecordDefinitionOffset,
                currentDefinition
            )
            || !TryRead(record + ModifierRecordExpiryOffset, expiry))
        {
            return false;
        }
        if (currentDefinition == definition)
        {
            instance.found = true;
            instance.record = record;
            instance.expiry = expiry;
            return true;
        }
        if (next == node)
        {
            return false;
        }
        node = next;
    }
    return node == 0;
}

bool FindProvinceModifierInstance(
    std::uintptr_t province,
    std::uintptr_t definition,
    ModifierInstance& instance
) noexcept
{
    return FindModifierInstance(
        province,
        ProvinceModifierListOffset,
        definition,
        instance
    );
}

bool FindCountryModifierInstance(
    std::uintptr_t country,
    std::uintptr_t definition,
    ModifierInstance& instance
) noexcept
{
    return FindModifierInstance(
        country,
        CountryModifierListOffset,
        definition,
        instance
    );
}

bool IsValidScriptIdentifier(std::string_view value) noexcept
{
    if (value.empty() || value.size() > MaximumScriptIdentifierLength)
    {
        return false;
    }
    return std::all_of(
        value.begin(),
        value.end(),
        [](unsigned char character)
        {
            return std::isalnum(character) != 0
                || character == '_'
                || character == '-'
                || character == '.'
                || character == ':';
        }
    );
}

bool SetBorrowedNativeString(
    std::string_view value,
    NativeBorrowedString32& native
) noexcept
{
    native = {};
    if (sizeof(void*) != sizeof(uint32_t)
        || value.empty()
        || value.size() > std::numeric_limits<uint32_t>::max())
    {
        return false;
    }
    const std::uintptr_t pointer = reinterpret_cast<std::uintptr_t>(
        value.data()
    );
    if (pointer > std::numeric_limits<uint32_t>::max())
    {
        return false;
    }
    native.pointer = static_cast<uint32_t>(pointer);
    native.size = static_cast<uint32_t>(value.size());
    native.capacity = 16;
    return true;
}

bool ReadGlobalFlag(
    const std::uint8_t* base,
    std::uintptr_t gameState,
    const char* name,
    bool& value
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    using Function = bool (__thiscall*)(void*, const char*);
    value = false;
    __try
    {
        value = reinterpret_cast<Function>(
            reinterpret_cast<std::uintptr_t>(base)
                + Profile.globalFlagQuery
        )(
            reinterpret_cast<void*>(
                gameState + GameStateGlobalFlagStoreOffset
            ),
            name
        );
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        value = false;
        return false;
    }
#else
    (void)base;
    (void)gameState;
    (void)name;
    value = false;
    return false;
#endif
}

bool InvokeGlobalFlagEffect(
    const std::uint8_t* base,
    std::string_view name,
    bool setFlag
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    GlobalFlagEffectObject effect{};
    if (!SetBorrowedNativeString(name, effect.name))
    {
        return false;
    }
    using Function = void (__thiscall*)(void*, void*, void*);
    __try
    {
        reinterpret_cast<Function>(
            reinterpret_cast<std::uintptr_t>(base)
                + (setFlag
                    ? Profile.setGlobalFlagEffectExecute
                    : Profile.clearGlobalFlagEffectExecute)
        )(&effect, nullptr, nullptr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    (void)base;
    (void)name;
    (void)setFlag;
    return false;
#endif
}

bool InitializeNativeEventScope(
    const std::uint8_t* base,
    const CountryTagValue& country,
    uint32_t provinceId,
    NativeEventScopeObject& scope
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    scope = {};
    void* const scopeAddress = &scope;
    const std::uintptr_t constructorAddress =
        reinterpret_cast<std::uintptr_t>(base)
        + Profile.eventScopeConstructor;
    __try
    {
        __asm
        {
            mov eax, scopeAddress
            xor edx, edx
            push 0
            call dword ptr [constructorAddress]
        }
        scope.country = country;
        scope.provinceId = provinceId;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        scope = {};
        return false;
    }
#else
    (void)base;
    (void)country;
    (void)provinceId;
    scope = {};
    return false;
#endif
}

void* FindEventDefinition(
    const std::uint8_t* base,
    int32_t eventId
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    std::uintptr_t registry = 0;
    if (!TryRead(
            reinterpret_cast<std::uintptr_t>(base)
                + Profile.eventRegistrySingleton,
            registry
        )
        || !registry)
    {
        return nullptr;
    }
    NativeEventLookupKey key{};
    key.id = eventId;
    void* definition = nullptr;
    const std::uintptr_t lookupAddress =
        reinterpret_cast<std::uintptr_t>(base) + Profile.eventLookup;
    __try
    {
        __asm
        {
            mov ecx, registry
            lea eax, key
            call dword ptr [lookupAddress]
            mov definition, eax
        }
        return definition;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return nullptr;
    }
#else
    (void)base;
    (void)eventId;
    return nullptr;
#endif
}

bool InvokeEventFire(
    const std::uint8_t* base,
    void* eventDefinition,
    NativeEventScopeObject& scope
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    using Function = void (__stdcall*)(void*, void*);
    __try
    {
        reinterpret_cast<Function>(
            reinterpret_cast<std::uintptr_t>(base) + Profile.eventFire
        )(eventDefinition, &scope);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    (void)base;
    (void)eventDefinition;
    (void)scope;
    return false;
#endif
}

bool InvokeDecisionCommand(
    const std::uint8_t* base,
    std::string_view decision,
    const CountryTagValue& country,
    uint32_t provinceId
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    NativeDecisionCommandObject command{};
    if (!SetBorrowedNativeString(decision, command.name)
        || !InitializeNativeEventScope(
            base,
            country,
            provinceId,
            command.scope
        ))
    {
        return false;
    }
    using Function = void (__thiscall*)(void*);
    __try
    {
        reinterpret_cast<Function>(
            reinterpret_cast<std::uintptr_t>(base)
                + Profile.decisionCommandExecute
        )(&command);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    (void)base;
    (void)decision;
    (void)country;
    (void)provinceId;
    return false;
#endif
}

bool ReadCurrentTotalDays(
    const std::uint8_t* base,
    std::uintptr_t gameState,
    int32_t& days
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    using Function = int32_t (__thiscall*)(void*);
    days = 0;
    __try
    {
        days = reinterpret_cast<Function>(
            reinterpret_cast<std::uintptr_t>(base)
                + Profile.currentDateTotalDays
        )(reinterpret_cast<void*>(gameState + CurrentDateOffset));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        days = 0;
        return false;
    }
#else
    (void)base;
    (void)gameState;
    days = 0;
    return false;
#endif
}

bool InvokeCountryValueEffect(
    const std::uint8_t* base,
    std::uintptr_t executeRva,
    uint32_t countryIndex,
    int32_t amount
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    CountryEffectObject effect{};
    CountryEffectScope scope{};
    effect.amount = amount;
    scope.countryIndex = countryIndex;
    using Function = void (__thiscall*)(void*, void*, void*);
    __try
    {
        reinterpret_cast<Function>(
            reinterpret_cast<std::uintptr_t>(base)
                + executeRva
        )(&effect, &scope, nullptr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    (void)base;
    (void)executeRva;
    (void)countryIndex;
    (void)amount;
    return false;
#endif
}

bool CountryTagsEqual(
    const CountryTagValue& left,
    const CountryTagValue& right
) noexcept
{
    return left.tag == right.tag && left.index == right.index;
}

bool InvokeNativeCommand(
    const std::uint8_t* base,
    std::uintptr_t executeRva,
    void* command
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    using Function = void (__thiscall*)(void*);
    __try
    {
        reinterpret_cast<Function>(
            reinterpret_cast<std::uintptr_t>(base) + executeRva
        )(command);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    (void)base;
    (void)executeRva;
    (void)command;
    return false;
#endif
}

bool InvokeProvinceOwnerSetter(
    const std::uint8_t* base,
    std::uintptr_t gameState,
    std::uintptr_t province,
    const CountryTagValue& country
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    using Function = void (__thiscall*)(
        void*,
        const CountryTagValue*,
        void*,
        bool,
        bool,
        bool
    );
    __try
    {
        reinterpret_cast<Function>(
            reinterpret_cast<std::uintptr_t>(base)
                + Profile.provinceOwnerSetter
        )(
            reinterpret_cast<void*>(province),
            &country,
            reinterpret_cast<void*>(gameState + CurrentDateOffset),
            true,
            true,
            false
        );
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    (void)base;
    (void)gameState;
    (void)province;
    (void)country;
    return false;
#endif
}

bool InvokeChangeControllerEffect(
    const std::uint8_t* base,
    uint32_t provinceId,
    const CountryTagValue& country
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    ProvinceCountryEffectObject effect{};
    ProvinceEffectScope scope{};
    effect.country = country;
    scope.provinceId = provinceId;
    using Function = void (__thiscall*)(void*, void*, void*);
    __try
    {
        reinterpret_cast<Function>(
            reinterpret_cast<std::uintptr_t>(base)
                + Profile.changeControllerEffectExecute
        )(&effect, &scope, nullptr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    (void)base;
    (void)provinceId;
    (void)country;
    return false;
#endif
}

bool InvokeProvinceCoreEffect(
    const std::uint8_t* base,
    uint32_t provinceId,
    const CountryTagValue& country,
    bool addCore
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    TargetedProvinceEffectScope scope{};
    scope.country = country;
    scope.provinceId = provinceId;
    using Function = void (__thiscall*)(void*, void*, void*);
    __try
    {
        if (addCore)
        {
            AddCoreEffectObject effect{};
            effect.country = country;
            effect.provinceId = provinceId;
            reinterpret_cast<Function>(
                reinterpret_cast<std::uintptr_t>(base)
                    + Profile.addCoreEffectExecute
            )(&effect, &scope, nullptr);
        }
        else
        {
            RemoveCoreEffectObject effect{};
            effect.country = country;
            effect.provinceId = provinceId;
            reinterpret_cast<Function>(
                reinterpret_cast<std::uintptr_t>(base)
                    + Profile.removeCoreEffectExecute
            )(&effect, &scope, nullptr);
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    (void)base;
    (void)provinceId;
    (void)country;
    (void)addCore;
    return false;
#endif
}

bool InvokeBuildingLevelCommand(
    const std::uint8_t* base,
    uint32_t provinceId,
    std::uintptr_t buildingDefinition,
    int32_t level
) noexcept
{
    BuildingLevelCommandObject command{};
    command.buildingDefinition = static_cast<uint32_t>(buildingDefinition);
    command.level = level;
    command.provinceId = provinceId;
    return InvokeNativeCommand(
        base,
        Profile.buildingLevelCommandExecute,
        &command
    );
}

bool InvokeBuildingRecalculate(
    const std::uint8_t* base,
    std::uintptr_t buildingRecord,
    std::uintptr_t province
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    using Function = void (__stdcall*)(void*);
    __try
    {
        reinterpret_cast<Function>(
            reinterpret_cast<std::uintptr_t>(base)
                + Profile.buildingRecordRecalculate
        )(reinterpret_cast<void*>(buildingRecord));
        reinterpret_cast<Function>(
            reinterpret_cast<std::uintptr_t>(base)
                + Profile.provinceRecalculate
        )(reinterpret_cast<void*>(province));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    (void)base;
    (void)buildingRecord;
    (void)province;
    return false;
#endif
}

bool RestoreBuildingLevels(
    const std::uint8_t* base,
    std::uintptr_t province,
    std::uintptr_t buildingRecord,
    int32_t completed,
    int32_t maximum
) noexcept
{
    return TryWrite(
            buildingRecord + BuildingRecordCompletedLevelOffset,
            completed
        )
        && TryWrite(
            buildingRecord + BuildingRecordMaximumLevelOffset,
            maximum
        )
        && InvokeBuildingRecalculate(
            base,
            buildingRecord,
            province
        );
}

bool InvokeTechnologyLevelCommand(
    const std::uint8_t* base,
    const CountryTagValue& country,
    std::uintptr_t technologyDefinition,
    int32_t level
) noexcept
{
    TechnologyLevelCommandObject command{};
    command.country = country;
    command.technologyDefinition = static_cast<uint32_t>(
        technologyDefinition
    );
    command.level = level;
    return InvokeNativeCommand(
        base,
        Profile.technologyLevelCommandExecute,
        &command
    );
}

bool InvokeResearchProgressCommand(
    const std::uint8_t* base,
    const CountryTagValue& country,
    std::uintptr_t technologyDefinition,
    uint64_t progress
) noexcept
{
    ResearchProgressCommandObject command{};
    command.country = country;
    command.technologyDefinition = static_cast<uint32_t>(
        technologyDefinition
    );
    command.progress = progress;
    return InvokeNativeCommand(
        base,
        Profile.researchProgressCommandExecute,
        &command
    );
}

bool InvokeResearchListCommand(
    const std::uint8_t* base,
    std::uintptr_t executeRva,
    const CountryTagValue& country,
    std::uintptr_t technologyDefinition
) noexcept
{
    CountryTechnologyCommandObject command{};
    command.country = country;
    command.technologyDefinition = static_cast<uint32_t>(
        technologyDefinition
    );
    return InvokeNativeCommand(base, executeRva, &command);
}

bool InvokeTechnologyInvestmentEffect(
    const std::uint8_t* base,
    uint32_t countryIndex,
    std::uintptr_t technologyDefinition,
    int32_t amount
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    TechnologyInvestmentEffectObject effect{};
    CountryEffectScope scope{};
    effect.amount = amount;
    effect.technologyDefinition = static_cast<uint32_t>(
        technologyDefinition
    );
    scope.countryIndex = countryIndex;
    using Function = void (__thiscall*)(void*, void*, void*);
    __try
    {
        reinterpret_cast<Function>(
            reinterpret_cast<std::uintptr_t>(base)
                + Profile.technologyInvestmentEffectExecute
        )(&effect, &scope, nullptr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    (void)base;
    (void)countryIndex;
    (void)technologyDefinition;
    (void)amount;
    return false;
#endif
}

bool InvokeCapitalEffect(
    const std::uint8_t* base,
    uint32_t countryIndex,
    uint32_t provinceId
) noexcept
{
    if (provinceId > static_cast<uint32_t>(
            std::numeric_limits<int32_t>::max() / FixedPointScale
        ))
    {
        return false;
    }
#if defined(_MSC_VER) && defined(_M_IX86)
    CapitalEffectObject effect{};
    CountryEffectScope scope{};
    effect.provinceIdFixed = static_cast<int32_t>(provinceId)
        * FixedPointScale;
    scope.countryIndex = countryIndex;
    using Function = void (__thiscall*)(void*, void*, void*);
    __try
    {
        reinterpret_cast<Function>(
            reinterpret_cast<std::uintptr_t>(base)
                + Profile.capitalEffectExecute
        )(&effect, &scope, nullptr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    (void)base;
    (void)countryIndex;
    (void)provinceId;
    return false;
#endif
}

bool InvokeActingCapitalSetter(
    const std::uint8_t* base,
    std::uintptr_t country,
    uint32_t provinceId
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    using Function = void (__stdcall*)(void*, uint32_t, bool);
    __try
    {
        reinterpret_cast<Function>(
            reinterpret_cast<std::uintptr_t>(base)
                + Profile.actingCapitalSetter
        )(reinterpret_cast<void*>(country), provinceId, true);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    (void)base;
    (void)country;
    (void)provinceId;
    return false;
#endif
}

bool ReadProvinceCountry(
    std::uintptr_t province,
    std::uintptr_t offset,
    CountryTagValue& country
) noexcept
{
    return TryRead(province + offset, country)
        && country.index < MaximumCountryCount;
}

bool FindProvinceCore(
    std::uintptr_t province,
    uint32_t countryIndex,
    bool& found
) noexcept
{
    found = false;
    std::uintptr_t node = 0;
    if (countryIndex >= MaximumCountryCount
        || !TryRead(province + ProvinceCoreListOffset, node))
    {
        return false;
    }
    for (std::size_t guard = 0;
         node && guard < MaximumCoreNodes;
         ++guard)
    {
        uint32_t currentCountryIndex = 0;
        std::uintptr_t next = 0;
        if (!TryRead(
                node + ProvinceCoreNodeCountryIndexOffset,
                currentCountryIndex
            )
            || !TryRead(node + ProvinceCoreNodeNextOffset, next)
            || next == node)
        {
            return false;
        }
        if (currentCountryIndex == countryIndex)
        {
            found = true;
            return true;
        }
        node = next;
    }
    return node == 0;
}

bool ResolveBuildingRecord(
    std::uintptr_t province,
    std::uintptr_t definition,
    std::uintptr_t& record,
    std::string& error
) noexcept
{
    uint32_t index = 0;
    std::uintptr_t table = 0;
    record = 0;
    if (!TryRead(definition + BuildingDefinitionIndexOffset, index)
        || index >= MaximumCountryCount
        || !TryRead(province + ProvinceBuildingTableOffset, table)
        || !table
        || !TryRead(
            table + static_cast<std::uintptr_t>(index)
                * sizeof(std::uint32_t),
            record
        )
        || !record)
    {
        error = "hoi3_province_building_record_unavailable";
        record = 0;
        return false;
    }
    int32_t completed = 0;
    int32_t maximum = 0;
    if (!TryRead(
            record + BuildingRecordCompletedLevelOffset,
            completed
        )
        || !TryRead(
            record + BuildingRecordMaximumLevelOffset,
            maximum
        ))
    {
        error = "hoi3_province_building_level_unavailable";
        record = 0;
        return false;
    }
    return true;
}

struct TechnologyStateEntry
{
    std::uintptr_t status = 0;
    std::uintptr_t levelField = 0;
    std::uintptr_t progressField = 0;
    uint32_t index = 0;
    int32_t level = 0;
    uint64_t progress = 0;
    int32_t maximumLevel = 0;
};

bool ResolveTechnologyStateEntry(
    std::uintptr_t country,
    std::uintptr_t definition,
    TechnologyStateEntry& entry,
    std::string& error
) noexcept
{
    entry = {};
    std::uintptr_t levels = 0;
    std::uintptr_t progress = 0;
    int32_t oneLevelOnly = 0;
    int32_t configuredMaximum = 0;
    if (!TryRead(definition + TechnologyDefinitionIndexOffset, entry.index)
        || entry.index >= MaximumProvinceCount
        || !TryRead(
            definition + TechnologyOneLevelOnlyOffset,
            oneLevelOnly
        )
        || !TryRead(
            definition + TechnologyMaximumLevelOffset,
            configuredMaximum
        )
        || configuredMaximum < 0
        || !TryRead(country + CountryTechnologyModifierOffset, entry.status)
        || !entry.status
        || !TryRead(entry.status + TechnologyStatusLevelsOffset, levels)
        || !levels
        || !TryRead(entry.status + TechnologyStatusProgressOffset, progress)
        || !progress)
    {
        error = "hoi3_technology_state_unavailable";
        entry = {};
        return false;
    }
    entry.levelField = levels + static_cast<std::uintptr_t>(entry.index)
        * sizeof(int32_t);
    entry.progressField = progress + static_cast<std::uintptr_t>(entry.index)
        * sizeof(uint64_t);
    entry.maximumLevel = oneLevelOnly == 0
        ? std::min(configuredMaximum, 1)
        : configuredMaximum;
    if (!TryRead(entry.levelField, entry.level)
        || !TryRead(entry.progressField, entry.progress))
    {
        error = "hoi3_technology_state_unavailable";
        entry = {};
        return false;
    }
    return true;
}

bool FindResearchEntry(
    std::uintptr_t country,
    std::uintptr_t technologyDefinition,
    bool& found
) noexcept
{
    found = false;
    std::uintptr_t node = 0;
    int32_t count = 0;
    if (!TryRead(country + CountryResearchListHeadOffset, node)
        || !TryRead(country + CountryResearchListCountOffset, count)
        || count < 0
        || count > static_cast<int32_t>(MaximumResearchNodes))
    {
        return false;
    }
    for (std::size_t guard = 0;
         node && guard < MaximumResearchNodes;
         ++guard)
    {
        std::uintptr_t currentTechnology = 0;
        std::uintptr_t next = 0;
        if (!TryRead(
                node + ResearchNodeTechnologyOffset,
                currentTechnology
            )
            || !TryRead(node + ResearchNodeNextOffset, next)
            || next == node)
        {
            return false;
        }
        if (currentTechnology == technologyDefinition)
        {
            found = true;
            return true;
        }
        node = next;
    }
    return node == 0;
}

bool InvokeDiplomaticInfluenceAdd(
    const std::uint8_t* base,
    std::uintptr_t country,
    int32_t amount
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(base)
        + Profile.diplomaticInfluenceAdd;
    __try
    {
        __asm
        {
            mov eax, country
            push amount
            mov edx, address
            call edx
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    (void)base;
    (void)country;
    (void)amount;
    return false;
#endif
}

bool InvokeProvinceLeadershipSetter(
    const std::uint8_t* base,
    std::uintptr_t province,
    int32_t value
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    using Function = void (__stdcall*)(void*, int32_t, bool);
    __try
    {
        reinterpret_cast<Function>(
            reinterpret_cast<std::uintptr_t>(base)
                + Profile.provinceLeadershipSetter
        )(reinterpret_cast<void*>(province), value, false);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    (void)base;
    (void)province;
    (void)value;
    return false;
#endif
}

bool InvokeCountryDefinitionSetter(
    const std::uint8_t* base,
    std::uintptr_t setterRva,
    std::uintptr_t country,
    std::uintptr_t definition
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    using Function = void (__stdcall*)(void*, void*, bool);
    __try
    {
        reinterpret_cast<Function>(
            reinterpret_cast<std::uintptr_t>(base) + setterRva
        )(
            reinterpret_cast<void*>(country),
            reinterpret_cast<void*>(definition),
            true
        );
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    (void)base;
    (void)setterRva;
    (void)country;
    (void)definition;
    return false;
#endif
}

bool InvokeGovernmentRevalidate(
    const std::uint8_t* base,
    std::uintptr_t country
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    using Function = void (__stdcall*)(void*, bool, bool);
    __try
    {
        reinterpret_cast<Function>(
            reinterpret_cast<std::uintptr_t>(base)
                + Profile.countryGovernmentRevalidate
        )(reinterpret_cast<void*>(country), true, false);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    (void)base;
    (void)country;
    return false;
#endif
}

bool InvokeIdeologyValuesEffect(
    const std::uint8_t* base,
    uint32_t countryIndex,
    std::uintptr_t ideologyDefinition,
    int32_t organizationAmount,
    int32_t popularityAmount
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    IdeologyValuesEffectObject effect{};
    CountryEffectScope scope{};
    effect.ideologyDefinition = static_cast<uint32_t>(ideologyDefinition);
    effect.organizationAmount = organizationAmount;
    effect.popularityAmount = popularityAmount;
    scope.countryIndex = countryIndex;
    using Function = void (__thiscall*)(void*, void*, void*);
    __try
    {
        reinterpret_cast<Function>(
            reinterpret_cast<std::uintptr_t>(base)
                + Profile.ideologyValuesEffectExecute
        )(&effect, &scope, nullptr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    (void)base;
    (void)countryIndex;
    (void)ideologyDefinition;
    (void)organizationAmount;
    (void)popularityAmount;
    return false;
#endif
}

bool InvokeRelationAdd(
    const std::uint8_t* base,
    std::uintptr_t country,
    const CountryTagValue& otherCountry,
    int32_t amount
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(base)
        + Profile.relationAdd;
    const CountryTagValue* const tag = &otherCountry;
    __try
    {
        __asm
        {
            push edi
            mov edi, country
            mov ecx, tag
            push amount
            mov eax, address
            call eax
            pop edi
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    (void)base;
    (void)country;
    (void)otherCountry;
    (void)amount;
    return false;
#endif
}

bool InvokeThreatEffect(
    const std::uint8_t* base,
    uint32_t sourceCountryIndex,
    uint32_t targetCountryIndex,
    int32_t amount
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    ThreatEffectObject effect{};
    CountryEffectScope scope{};
    effect.amount = amount;
    effect.countryIndex = sourceCountryIndex;
    scope.countryIndex = targetCountryIndex;
    using Function = void (__thiscall*)(void*, void*, void*);
    __try
    {
        reinterpret_cast<Function>(
            reinterpret_cast<std::uintptr_t>(base)
                + Profile.threatEffectExecute
        )(&effect, &scope, nullptr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    (void)base;
    (void)sourceCountryIndex;
    (void)targetCountryIndex;
    (void)amount;
    return false;
#endif
}

bool ResolveRelationRecord(
    std::uintptr_t country,
    const CountryTagValue& otherCountry,
    std::uintptr_t& relation,
    std::string& error,
    const std::shared_ptr<void>& safetyLease = {}
) noexcept
{
    relation = 0;
    std::string sourceTag;
    std::string targetTag;
    uint64_t stableId = 0;
    engine::ObjectHandle handle;
    if (!ReadCountryTag(country, sourceTag)
        || !UnpackHoi3CountryTag(otherCountry.tag, targetTag)
        || !PackHoi3RelationKey(sourceTag, targetTag, stableId)
        || !ResolveNativeObject(
            {engine::TypeId::Relation, stableId},
            safetyLease,
            handle,
            error
        ))
    {
        if (error.empty())
        {
            error = "hoi3_diplomacy_relation_unavailable";
        }
        return false;
    }
    relation = handle.address;
    return true;
}

bool ResolveSpyPresence(
    std::uintptr_t country,
    uint32_t targetCountryIndex,
    std::uintptr_t& presence
) noexcept
{
    presence = 0;
    std::uintptr_t table = 0;
    if (targetCountryIndex >= MaximumCountryCount
        || !TryRead(country + CountrySpyPresenceTableOffset, table)
        || !table)
    {
        return false;
    }
    const uint64_t offset = static_cast<uint64_t>(targetCountryIndex)
        * SpyPresenceRecordSize;
    if (offset > std::numeric_limits<std::uintptr_t>::max() - table)
    {
        return false;
    }
    presence = table + static_cast<std::uintptr_t>(offset);
    int32_t level = 0;
    return TryRead(presence + SpyPresenceLevelOffset, level);
}

bool ReadIdeologyValue(
    std::uintptr_t country,
    std::uintptr_t ideology,
    IdeologyValueKind kind,
    int32_t& value
) noexcept
{
    uint32_t ideologyIndex = 0;
    std::uintptr_t values = 0;
    const std::uintptr_t valuesOffset =
        kind == IdeologyValueKind::Popularity
        ? CountryIdeologyPopularityOffset
        : CountryIdeologyOrganizationOffset;
    return TryRead(ideology + IdeologyIndexOffset, ideologyIndex)
        && ideologyIndex < MaximumCountryCount
        && TryRead(country + valuesOffset, values)
        && values
        && TryRead(
            values + static_cast<std::uintptr_t>(ideologyIndex)
                * sizeof(int32_t),
            value
        );
}

bool ResolveProvinceIntelLevel(
    std::uintptr_t province,
    uint32_t countryIndex,
    std::uintptr_t& field
) noexcept
{
    field = 0;
    std::uintptr_t levels = 0;
    uint32_t count = 0;
    if (countryIndex >= MaximumCountryCount
        || !TryRead(province + ProvinceIntelLevelsOffset, levels)
        || !levels
        || !TryRead(province + ProvinceIntelLevelCountOffset, count)
        || count > MaximumCountryCount
        || countryIndex >= count)
    {
        return false;
    }
    field = levels + countryIndex;
    std::uint8_t current = 0;
    return TryRead(field, current);
}

int64_t MultiplyFixed(int64_t value, int64_t factor) noexcept
{
    return value * factor / FixedPointScale;
}

bool ComputeCountryGrossLeadership(
    std::uintptr_t gameState,
    std::uintptr_t country,
    std::uintptr_t sourceProvince,
    int32_t sourceValue,
    int64_t& total,
    const std::shared_ptr<void>& safetyLease
) noexcept
{
    std::uintptr_t node = 0;
    if (!TryRead(country + CountryOwnedProvinceListOffset, node))
    {
        return false;
    }
    total = 0;
    for (std::size_t guard = 0;
         node && guard < MaximumOwnedProvinceNodes;
         ++guard)
    {
        uint32_t provinceId = 0;
        std::uintptr_t next = 0;
        std::uintptr_t province = 0;
        std::uintptr_t modifier = 0;
        int32_t leadership = 0;
        int32_t additive = 0;
        int32_t multiplicative = 0;
        std::string provinceError;
        if (!TryRead(
                node + OwnedProvinceNodeProvinceIdOffset,
                provinceId
            )
            || !TryRead(node + OwnedProvinceNodeNextOffset, next)
            || !ResolveProvince(
                gameState,
                static_cast<int64_t>(provinceId),
                province,
                provinceError,
                safetyLease
            ))
        {
            return false;
        }
        if (!TryRead(province + ProvinceModifierCacheOffset, modifier)
            || !modifier
            || !TryRead(
                modifier + ProvinceLeadershipAdditiveModifierOffset,
                additive
            )
            || !TryRead(
                modifier + ProvinceLeadershipMultiplicativeModifierOffset,
                multiplicative
            ))
        {
            return false;
        }
        if (province == sourceProvince)
        {
            leadership = sourceValue;
        }
        else if (!TryRead(province + ProvinceLeadershipOffset, leadership))
        {
            return false;
        }
        const int64_t factor = std::max<int64_t>(
            0,
            static_cast<int64_t>(multiplicative) + FixedPointScale
        );
        total += std::max<int64_t>(
            0,
            MultiplyFixed(
                static_cast<int64_t>(leadership) + additive,
                factor
            )
        );
        if (total > std::numeric_limits<int32_t>::max())
        {
            return true;
        }
        if (next == node)
        {
            return false;
        }
        node = next;
    }
    if (node)
    {
        return false;
    }

    std::uintptr_t countryModifier = 0;
    std::uintptr_t technologyModifier = 0;
    int32_t additive = 0;
    int32_t multiplicative = 0;
    int32_t technology = 0;
    if (!TryRead(country + CountryModifierCacheOffset, countryModifier)
        || !countryModifier
        || !TryRead(
            countryModifier + CountryLeadershipAdditiveModifierOffset,
            additive
        )
        || !TryRead(
            countryModifier + CountryLeadershipMultiplicativeModifierOffset,
            multiplicative
        )
        || !TryRead(
            country + CountryTechnologyModifierOffset,
            technologyModifier
        )
        || !technologyModifier
        || !TryRead(
            technologyModifier + TechnologyLeadershipModifierOffset,
            technology
        ))
    {
        return false;
    }
    total += additive;
    total = MultiplyFixed(
        total,
        static_cast<int64_t>(multiplicative)
            + technology
            + FixedPointScale
    );
    total = std::max<int64_t>(FixedPointScale, total);
    return true;
}

bool FindLeadershipSourceValue(
    std::uintptr_t gameState,
    std::uintptr_t country,
    std::uintptr_t sourceProvince,
    int32_t oldSource,
    int32_t oldTotal,
    int32_t desiredTotal,
    int32_t& sourceValue,
    const std::shared_ptr<void>& safetyLease
) noexcept
{
    int64_t oldGross = 0;
    if (!ComputeCountryGrossLeadership(
            gameState,
            country,
            sourceProvince,
            oldSource,
            oldGross,
            safetyLease
        ))
    {
        return false;
    }
    const int64_t consumed = oldGross - oldTotal;
    const int64_t desiredGross = static_cast<int64_t>(desiredTotal)
        + consumed;
    if (desiredGross < FixedPointScale
        || desiredGross > std::numeric_limits<int32_t>::max())
    {
        return false;
    }

    int32_t low = 0;
    int32_t high = std::max<int32_t>(oldSource, FixedPointScale);
    int64_t highGross = 0;
    while (true)
    {
        if (!ComputeCountryGrossLeadership(
                gameState,
                country,
                sourceProvince,
                high,
                highGross,
                safetyLease
            ))
        {
            return false;
        }
        if (highGross >= desiredGross)
        {
            break;
        }
        if (high == std::numeric_limits<int32_t>::max())
        {
            return false;
        }
        const int64_t doubled = static_cast<int64_t>(high) * 2
            + FixedPointScale;
        high = static_cast<int32_t>(std::min<int64_t>(
            doubled,
            std::numeric_limits<int32_t>::max()
        ));
    }

    while (low <= high)
    {
        const int32_t candidate = low
            + static_cast<int32_t>(
                (static_cast<int64_t>(high) - low) / 2
            );
        int64_t candidateGross = 0;
        if (!ComputeCountryGrossLeadership(
                gameState,
                country,
                sourceProvince,
                candidate,
                candidateGross,
                safetyLease
            ))
        {
            return false;
        }
        if (candidateGross == desiredGross)
        {
            sourceValue = candidate;
            return true;
        }
        if (candidateGross < desiredGross)
        {
            if (candidate == std::numeric_limits<int32_t>::max())
            {
                break;
            }
            low = candidate + 1;
        }
        else
        {
            if (candidate == 0)
            {
                break;
            }
            high = candidate - 1;
        }
    }
    bool found = false;
    int64_t bestDistance = std::numeric_limits<int64_t>::max();
    const std::array<int32_t, 2> candidates = {high, low};
    for (const int32_t candidate : candidates)
    {
        if (candidate < 0)
        {
            continue;
        }
        int64_t candidateGross = 0;
        if (!ComputeCountryGrossLeadership(
                gameState,
                country,
                sourceProvince,
                candidate,
                candidateGross,
                safetyLease
            ))
        {
            return false;
        }
        const int64_t distance = candidateGross >= desiredGross
            ? candidateGross - desiredGross
            : desiredGross - candidateGross;
        if (!found || distance < bestDistance)
        {
            found = true;
            bestDistance = distance;
            sourceValue = candidate;
        }
    }
    return found;
}

std::uintptr_t InvokeCountryGoodsPoolResolver(
    const std::uint8_t* base,
    std::uintptr_t country
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    using Function = void* (__thiscall*)(void*);
    void* result = nullptr;
    __try
    {
        result = reinterpret_cast<Function>(
            reinterpret_cast<std::uintptr_t>(base)
                + Profile.countryGoodsPoolResolver
        )(reinterpret_cast<void*>(country));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        result = nullptr;
    }
    return reinterpret_cast<std::uintptr_t>(result);
#else
    (void)base;
    (void)country;
    return 0;
#endif
}

bool ResolveCountryGoodsPool(
    const std::uint8_t* base,
    std::uintptr_t country,
    std::uintptr_t& pool,
    std::string& error
) noexcept
{
    uint8_t usesEmbeddedPool = 0;
    pool = 0;
    if (!TryRead(
            country + CountryGoodsEmbeddedFlagOffset,
            usesEmbeddedPool
        ))
    {
        error = "hoi3_country_goods_pool_unavailable";
        return false;
    }
    if (usesEmbeddedPool != 0)
    {
        pool = country + CountryGoodsEmbeddedOffset;
        return true;
    }
    const std::uintptr_t resolved = InvokeCountryGoodsPoolResolver(
        base,
        country
    );
    if (!resolved)
    {
        error = "hoi3_country_goods_pool_unavailable";
        return false;
    }
    pool = resolved + ResolvedCountryGoodsPoolOffset;
    return true;
}

bool InvokeAddCountryModifierEffect(
    const std::uint8_t* base,
    uint32_t countryIndex,
    std::uintptr_t definition,
    int32_t durationDays
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    ProvinceAddEffectObject effect{};
    CountryEffectScope scope{};
    effect.durationDays = durationDays;
    effect.modifierDefinition = static_cast<uint32_t>(definition);
    scope.countryIndex = countryIndex;
    using Function = void (__thiscall*)(void*, void*, void*);
    __try
    {
        reinterpret_cast<Function>(
            reinterpret_cast<std::uintptr_t>(base)
                + Profile.addCountryModifierEffectExecute
        )(&effect, &scope, nullptr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    (void)base;
    (void)countryIndex;
    (void)definition;
    (void)durationDays;
    return false;
#endif
}

bool InvokeRemoveCountryModifierEffect(
    const std::uint8_t* base,
    uint32_t countryIndex,
    std::uintptr_t definition
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    ProvinceRemoveEffectObject effect{};
    CountryEffectScope scope{};
    effect.modifierDefinition = static_cast<uint32_t>(definition);
    scope.countryIndex = countryIndex;
    using Function = void (__thiscall*)(void*, void*, void*);
    __try
    {
        reinterpret_cast<Function>(
            reinterpret_cast<std::uintptr_t>(base)
                + Profile.removeCountryModifierEffectExecute
        )(&effect, &scope, nullptr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    (void)base;
    (void)countryIndex;
    (void)definition;
    return false;
#endif
}

bool InvokeAddProvinceModifierEffect(
    const std::uint8_t* base,
    uint32_t provinceId,
    std::uintptr_t definition,
    int32_t durationDays
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    ProvinceAddEffectObject effect{};
    ProvinceEffectScope scope{};
    effect.durationDays = durationDays;
    effect.modifierDefinition = static_cast<uint32_t>(definition);
    scope.provinceId = provinceId;
    using Function = void (__thiscall*)(void*, void*, void*);
    __try
    {
        reinterpret_cast<Function>(
            reinterpret_cast<std::uintptr_t>(base)
                + Profile.addProvinceModifierEffectExecute
        )(&effect, &scope, nullptr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    (void)base;
    (void)provinceId;
    (void)definition;
    (void)durationDays;
    return false;
#endif
}

bool InvokeRemoveProvinceModifierEffect(
    const std::uint8_t* base,
    uint32_t provinceId,
    std::uintptr_t definition
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    ProvinceRemoveEffectObject effect{};
    ProvinceEffectScope scope{};
    effect.modifierDefinition = static_cast<uint32_t>(definition);
    scope.provinceId = provinceId;
    using Function = void (__thiscall*)(void*, void*, void*);
    __try
    {
        reinterpret_cast<Function>(
            reinterpret_cast<std::uintptr_t>(base)
                + Profile.removeProvinceModifierEffectExecute
        )(&effect, &scope, nullptr);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    (void)base;
    (void)provinceId;
    (void)definition;
    return false;
#endif
}

bool RestoreProvinceModifier(
    const std::uint8_t* base,
    std::uintptr_t province,
    uint32_t provinceId,
    std::uintptr_t definition,
    int32_t expiry
) noexcept
{
    if (!InvokeAddProvinceModifierEffect(
            base,
            provinceId,
            definition,
            -1
        ))
    {
        return false;
    }
    ModifierInstance restored;
    return FindProvinceModifierInstance(province, definition, restored)
        && restored.found
        && TryWrite(restored.record + ModifierRecordExpiryOffset, expiry);
}

bool RestoreCountryModifier(
    const std::uint8_t* base,
    std::uintptr_t country,
    uint32_t countryIndex,
    std::uintptr_t definition,
    int32_t expiry
) noexcept
{
    if (!InvokeAddCountryModifierEffect(
            base,
            countryIndex,
            definition,
            -1
        ))
    {
        return false;
    }
    ModifierInstance restored;
    return FindCountryModifierInstance(country, definition, restored)
        && restored.found
        && TryWrite(restored.record + ModifierRecordExpiryOffset, expiry);
}

bool ComputeNationalUnityChange(
    int32_t nativeAmount,
    int32_t factor,
    int32_t& effectiveChange
) noexcept
{
    const int64_t value = static_cast<int64_t>(nativeAmount) * factor
        / FixedPointScale;
    if (value < std::numeric_limits<int32_t>::min()
        || value > std::numeric_limits<int32_t>::max())
    {
        return false;
    }
    effectiveChange = static_cast<int32_t>(value);
    return true;
}

bool FindNationalUnityNativeAmount(
    int32_t desiredChange,
    int32_t factor,
    int32_t& nativeAmount
) noexcept
{
    if (desiredChange == 0)
    {
        nativeAmount = 0;
        return true;
    }
    if (factor <= 0)
    {
        return false;
    }
    const long double estimate = static_cast<long double>(desiredChange)
        * FixedPointScale / factor;
    if (estimate < std::numeric_limits<int32_t>::min()
        || estimate > std::numeric_limits<int32_t>::max())
    {
        return false;
    }
    const int64_t center = static_cast<int64_t>(std::llround(estimate));
    constexpr int64_t SearchRadius = 2048;
    for (int64_t distance = 0; distance <= SearchRadius; ++distance)
    {
        const std::array<int64_t, 2> candidates = {
            center - distance,
            center + distance
        };
        for (const int64_t candidate : candidates)
        {
            if (candidate < std::numeric_limits<int32_t>::min()
                || candidate > std::numeric_limits<int32_t>::max())
            {
                continue;
            }
            int32_t change = 0;
            if (ComputeNationalUnityChange(
                    static_cast<int32_t>(candidate),
                    factor,
                    change
                )
                && change == desiredChange)
            {
                nativeAmount = static_cast<int32_t>(candidate);
                return true;
            }
        }
    }
    return false;
}

}

struct Hoi3GameplayEffects::Impl
{
    enum class DispatchKind
    {
        Event,
        Decision
    };

    struct ApplyState
    {
        bool applied = false;
    };

    struct QueuedDispatch
    {
        DispatchKind kind = DispatchKind::Event;
        uint64_t queueId = 0;
        uint64_t transactionId = 0;
        uint64_t lifecycleGeneration = 0;
        int32_t dueDay = 0;
        int32_t eventId = 0;
        std::string decision;
        std::string countryTag;
        uint32_t provinceId = 0;
    };

    std::uint8_t* executableBase = nullptr;
    NativeEffectService* service = nullptr;
    bool supported = false;
    std::vector<std::string> registeredOperations;
    std::mutex queueMutex;
    std::vector<QueuedDispatch> queuedDispatches;
    uint64_t nextQueueId = 1;

    bool ResolveDispatchTarget(
        const NativeEffect& effect,
        const NativeEffectExecutionContext& context,
        bool requirePlayer,
        std::uintptr_t& gameState,
        CountryTagValue& country,
        std::string& countryTag,
        uint32_t& provinceId,
        std::string& error
    ) const
    {
        countryTag = context.playerTag;
        if (const NativeEffectValue* value = FindFirst(
                effect,
                {"tag", "country_tag", "countrytag"}
            ))
        {
            if (!NativeEffectValueToString(*value, countryTag))
            {
                error = "dispatch_country_tag_invalid";
                return false;
            }
        }
        countryTag = UpperTag(std::move(countryTag));
        if (countryTag.size() != 3)
        {
            error = "dispatch_country_tag_invalid";
            return false;
        }
        if (requirePlayer
            && countryTag != UpperTag(context.playerTag))
        {
            error = "decision_target_not_player";
            return false;
        }

        std::uintptr_t countryAddress = 0;
        if (!ResolveGameState(executableBase, gameState)
            || !ResolveCountryByTag(
                executableBase,
                countryTag,
                countryAddress,
                country,
                error,
                context.safetyLease
            ))
        {
            if (error.empty())
            {
                error = "hoi3_game_state_unavailable";
            }
            return false;
        }
        if (requirePlayer)
        {
            uint32_t playerCountryIndex = 0;
            if (!TryRead(
                    gameState + PlayerCountryIndexOffset,
                    playerCountryIndex
                )
                || playerCountryIndex != country.index)
            {
                error = "decision_target_not_player";
                return false;
            }
        }

        int64_t requestedProvinceId = 0;
        if (const NativeEffectValue* value = FindFirst(
                effect,
                {"province_id", "provinceid", "province"}
            ))
        {
            if (!NativeEffectValueToInteger(*value, requestedProvinceId)
                || requestedProvinceId < 0
                || requestedProvinceId >=
                    static_cast<int64_t>(MaximumProvinceCount))
            {
                error = "dispatch_province_id_invalid";
                return false;
            }
            if (requestedProvinceId > 0)
            {
                std::uintptr_t province = 0;
                if (!ResolveProvince(
                        gameState,
                        requestedProvinceId,
                        province,
                        error,
                        context.safetyLease
                    ))
                {
                    return false;
                }
            }
        }
        provinceId = static_cast<uint32_t>(requestedProvinceId);
        error.clear();
        return true;
    }

    bool PrepareGlobalFlag(
        const NativeEffect& effect,
        PreparedNativeEffect& prepared,
        std::string& error,
        bool setFlag
    ) const
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        std::string name;
        if (!ReadRequiredString(
                effect,
                {"name", "flag", "global_flag"},
                name,
                "global_flag_name_invalid",
                error
            )
            || !IsValidScriptIdentifier(name))
        {
            error = "global_flag_name_invalid";
            return false;
        }
        std::uintptr_t gameState = 0;
        bool original = false;
        if (!ResolveGameState(executableBase, gameState)
            || !ReadGlobalFlag(
                executableBase,
                gameState,
                name.c_str(),
                original
            ))
        {
            error = "hoi3_global_flag_state_unavailable";
            return false;
        }

        const auto state = std::make_shared<ApplyState>();
        std::uint8_t* const base = executableBase;
        prepared.apply = [
            base,
            gameState,
            name,
            original,
            setFlag,
            state
        ](std::string& applyError)
        {
            std::uintptr_t currentGameState = 0;
            bool current = false;
            if (!ResolveGameState(base, currentGameState)
                || currentGameState != gameState
                || !ReadGlobalFlag(
                    base,
                    gameState,
                    name.c_str(),
                    current
                )
                || current != original)
            {
                applyError = "global_flag_changed_before_apply";
                return false;
            }
            if (current != setFlag
                && !InvokeGlobalFlagEffect(base, name, setFlag))
            {
                applyError = "global_flag_native_apply_failed";
                return false;
            }
            bool verified = false;
            if (!ReadGlobalFlag(
                    base,
                    gameState,
                    name.c_str(),
                    verified
                )
                || verified != setFlag)
            {
                applyError = "global_flag_postcondition_failed";
                return false;
            }
            state->applied = original != setFlag;
            applyError.clear();
            return true;
        };
        prepared.rollback = [base, gameState, name, original, state]
        {
            if (!state->applied)
            {
                return;
            }
            std::uintptr_t currentGameState = 0;
            if (ResolveGameState(base, currentGameState)
                && currentGameState == gameState)
            {
                InvokeGlobalFlagEffect(base, name, original);
            }
            state->applied = false;
        };
        error.clear();
        return true;
    }

    bool PrepareEventFire(
        const NativeEffect& effect,
        const NativeEffectExecutionContext& context,
        PreparedNativeEffect& prepared,
        std::string& error
    ) const
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        int64_t requestedEventId = 0;
        if (!ReadRequiredInteger(
                effect,
                {"id", "event_id", "eventid"},
                requestedEventId,
                "event_id_invalid",
                error
            )
            || requestedEventId <= 0
            || requestedEventId > std::numeric_limits<int32_t>::max())
        {
            error = "event_id_invalid";
            return false;
        }
        std::uintptr_t gameState = 0;
        CountryTagValue country{};
        std::string countryTag;
        uint32_t provinceId = 0;
        if (!ResolveDispatchTarget(
                effect,
                context,
                false,
                gameState,
                country,
                countryTag,
                provinceId,
                error
            )
            || !FindEventDefinition(
                executableBase,
                static_cast<int32_t>(requestedEventId)
            ))
        {
            if (error.empty())
            {
                error = "event_definition_not_found";
            }
            return false;
        }

        std::uint8_t* const base = executableBase;
        const int32_t eventId = static_cast<int32_t>(requestedEventId);
        prepared.apply = [
            base,
            gameState,
            country,
            provinceId,
            eventId
        ](std::string& applyError)
        {
            std::uintptr_t currentGameState = 0;
            NativeEventScopeObject scope{};
            void* const definition = FindEventDefinition(base, eventId);
            if (!ResolveGameState(base, currentGameState)
                || currentGameState != gameState
                || !definition
                || !InitializeNativeEventScope(
                    base,
                    country,
                    provinceId,
                    scope
                )
                || !InvokeEventFire(base, definition, scope))
            {
                applyError = "event_native_dispatch_failed";
                return false;
            }
            applyError.clear();
            return true;
        };
        prepared.rollback = {};
        error.clear();
        return true;
    }

    bool PrepareDecisionExecute(
        const NativeEffect& effect,
        const NativeEffectExecutionContext& context,
        PreparedNativeEffect& prepared,
        std::string& error
    ) const
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        std::string decision;
        if (!ReadRequiredString(
                effect,
                {"name", "decision", "decision_key"},
                decision,
                "decision_name_invalid",
                error
            )
            || !IsValidScriptIdentifier(decision))
        {
            error = "decision_name_invalid";
            return false;
        }
        std::uintptr_t gameState = 0;
        CountryTagValue country{};
        std::string countryTag;
        uint32_t provinceId = 0;
        if (!ResolveDispatchTarget(
                effect,
                context,
                true,
                gameState,
                country,
                countryTag,
                provinceId,
                error
            ))
        {
            return false;
        }

        std::uint8_t* const base = executableBase;
        prepared.apply = [
            base,
            gameState,
            country,
            provinceId,
            decision
        ](std::string& applyError)
        {
            std::uintptr_t currentGameState = 0;
            if (!ResolveGameState(base, currentGameState)
                || currentGameState != gameState
                || !InvokeDecisionCommand(
                    base,
                    decision,
                    country,
                    provinceId
                ))
            {
                applyError = "decision_native_execute_failed";
                return false;
            }
            applyError.clear();
            return true;
        };
        prepared.rollback = {};
        error.clear();
        return true;
    }

    bool PrepareDispatchEnqueue(
        const NativeEffect& effect,
        const NativeEffectExecutionContext& context,
        PreparedNativeEffect& prepared,
        std::string& error,
        DispatchKind kind
    )
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        int64_t delayDays = 0;
        if (const NativeEffectValue* value = FindFirst(
                effect,
                {"delay_days", "delaydays", "delay"}
            ))
        {
            if (!NativeEffectValueToInteger(*value, delayDays)
                || delayDays < 0
                || delayDays > MaximumDispatchDelayDays)
            {
                error = "dispatch_delay_days_invalid";
                return false;
            }
        }

        QueuedDispatch dispatch{};
        dispatch.kind = kind;
        dispatch.transactionId = context.transactionId;
        dispatch.lifecycleGeneration = context.lifecycleGeneration;
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            dispatch.queueId = nextQueueId++;
        }
        std::uintptr_t gameState = 0;
        CountryTagValue country{};
        if (!ResolveDispatchTarget(
                effect,
                context,
                kind == DispatchKind::Decision,
                gameState,
                country,
                dispatch.countryTag,
                dispatch.provinceId,
                error
            ))
        {
            return false;
        }
        int32_t currentDay = 0;
        if (!ReadCurrentTotalDays(executableBase, gameState, currentDay)
            || (static_cast<int64_t>(currentDay) + delayDays)
                > std::numeric_limits<int32_t>::max())
        {
            error = "hoi3_current_date_unavailable";
            return false;
        }
        dispatch.dueDay = static_cast<int32_t>(
            static_cast<int64_t>(currentDay) + delayDays
        );
        if (kind == DispatchKind::Event)
        {
            int64_t eventId = 0;
            if (!ReadRequiredInteger(
                    effect,
                    {"id", "event_id", "eventid"},
                    eventId,
                    "event_id_invalid",
                    error
                )
                || eventId <= 0
                || eventId > std::numeric_limits<int32_t>::max()
                || (eventId <= std::numeric_limits<int32_t>::max()
                    && !FindEventDefinition(
                    executableBase,
                    static_cast<int32_t>(eventId)
                )))
            {
                error = eventId > 0
                        && eventId <= std::numeric_limits<int32_t>::max()
                    ? "event_definition_not_found" : "event_id_invalid";
                return false;
            }
            dispatch.eventId = static_cast<int32_t>(eventId);
        }
        else if (!ReadRequiredString(
                    effect,
                    {"name", "decision", "decision_key"},
                    dispatch.decision,
                    "decision_name_invalid",
                    error
                )
            || !IsValidScriptIdentifier(dispatch.decision))
        {
            error = "decision_name_invalid";
            return false;
        }

        const auto applied = std::make_shared<ApplyState>();
        prepared.apply = [this, dispatch, applied](std::string& applyError)
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            if (queuedDispatches.size() >= MaximumQueuedDispatches)
            {
                applyError = "dispatch_queue_full";
                return false;
            }
            queuedDispatches.push_back(dispatch);
            applied->applied = true;
            applyError.clear();
            return true;
        };
        prepared.rollback = [this, queueId = dispatch.queueId, applied]
        {
            if (!applied->applied)
            {
                return;
            }
            std::lock_guard<std::mutex> lock(queueMutex);
            queuedDispatches.erase(
                std::remove_if(
                    queuedDispatches.begin(),
                    queuedDispatches.end(),
                    [queueId](const QueuedDispatch& current)
                    {
                        return current.queueId == queueId;
                    }
                ),
                queuedDispatches.end()
            );
            applied->applied = false;
        };
        error.clear();
        return true;
    }

    bool PrepareDispatchCancel(
        const NativeEffect& effect,
        PreparedNativeEffect& prepared,
        std::string& error,
        std::optional<DispatchKind> requiredKind
    )
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        const NativeEffectValue* transactionValue = FindFirst(
            effect,
            {"transaction_id", "transactionid"}
        );
        const bool cancelTransaction = transactionValue != nullptr;
        const NativeEffectValue* identifierValue = transactionValue
            ? transactionValue
            : FindFirst(effect, {"queue_id", "queueid", "id"});
        int64_t requestedIdentifier = 0;
        if (!identifierValue
            || !NativeEffectValueToInteger(
                *identifierValue,
                requestedIdentifier
            )
            || requestedIdentifier <= 0)
        {
            error = "dispatch_queue_id_invalid";
            return false;
        }
        const uint64_t identifier = static_cast<uint64_t>(
            requestedIdentifier
        );
        const auto matches = [identifier, cancelTransaction, requiredKind](
            const QueuedDispatch& item
        )
        {
            return (cancelTransaction
                    ? item.transactionId == identifier
                    : item.queueId == identifier)
                && (!requiredKind || item.kind == *requiredKind);
        };
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            const auto current = std::find_if(
                queuedDispatches.begin(),
                queuedDispatches.end(),
                matches
            );
            if (current == queuedDispatches.end())
            {
                error = "dispatch_queue_item_not_found";
                return false;
            }
        }

        const auto removed = std::make_shared<std::vector<QueuedDispatch>>();
        prepared.apply = [
            this,
            matches,
            removed
        ](std::string& applyError)
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            for (auto current = queuedDispatches.begin();
                 current != queuedDispatches.end();)
            {
                if (matches(*current))
                {
                    removed->push_back(std::move(*current));
                    current = queuedDispatches.erase(current);
                }
                else
                {
                    ++current;
                }
            }
            if (removed->empty())
            {
                applyError = "dispatch_queue_item_not_found";
                return false;
            }
            applyError.clear();
            return true;
        };
        prepared.rollback = [this, removed]
        {
            if (removed->empty())
            {
                return;
            }
            std::lock_guard<std::mutex> lock(queueMutex);
            for (QueuedDispatch& dispatch : *removed)
            {
                queuedDispatches.push_back(std::move(dispatch));
            }
            removed->clear();
        };
        error.clear();
        return true;
    }

    std::vector<std::string> TickDispatchQueue()
    {
        std::vector<std::string> diagnostics;
        if (!supported || !executableBase)
        {
            return diagnostics;
        }
        std::uintptr_t gameState = 0;
        int32_t currentDay = 0;
        if (!ResolveGameState(executableBase, gameState)
            || !ReadCurrentTotalDays(executableBase, gameState, currentDay))
        {
            return diagnostics;
        }

        std::vector<QueuedDispatch> due;
        {
            std::lock_guard<std::mutex> lock(queueMutex);
            for (auto current = queuedDispatches.begin();
                 current != queuedDispatches.end();)
            {
                if (current->dueDay <= currentDay)
                {
                    due.push_back(std::move(*current));
                    current = queuedDispatches.erase(current);
                }
                else
                {
                    ++current;
                }
            }
        }

        for (const QueuedDispatch& dispatch : due)
        {
            std::uintptr_t countryAddress = 0;
            CountryTagValue country{};
            std::string resolveError;
            bool succeeded = ResolveCountryByTag(
                executableBase,
                dispatch.countryTag,
                countryAddress,
                country,
                resolveError
            );
            if (succeeded && dispatch.kind == DispatchKind::Event)
            {
                NativeEventScopeObject scope{};
                void* const definition = FindEventDefinition(
                    executableBase,
                    dispatch.eventId
                );
                succeeded = definition
                    && InitializeNativeEventScope(
                        executableBase,
                        country,
                        dispatch.provinceId,
                        scope
                    )
                    && InvokeEventFire(
                        executableBase,
                        definition,
                        scope
                    );
            }
            else if (succeeded)
            {
                uint32_t playerCountryIndex = 0;
                succeeded = TryRead(
                    gameState + PlayerCountryIndexOffset,
                    playerCountryIndex
                )
                    && playerCountryIndex == country.index
                    && InvokeDecisionCommand(
                        executableBase,
                        dispatch.decision,
                        country,
                        dispatch.provinceId
                    );
            }
            diagnostics.push_back(
                std::string(succeeded
                    ? "native dispatch queue applied: "
                    : "native dispatch queue failed: ")
                + std::to_string(dispatch.queueId)
                + (dispatch.kind == DispatchKind::Event
                    ? " event" : " decision")
            );
        }
        return diagnostics;
    }

    std::size_t ClearDispatchQueue()
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        const std::size_t count = queuedDispatches.size();
        queuedDispatches.clear();
        return count;
    }

    bool PrepareProvinceCountryAssignment(
        const NativeEffect& effect,
        const NativeEffectExecutionContext& context,
        PreparedNativeEffect& prepared,
        std::string& error,
        bool setOwner
    ) const
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        int64_t provinceId = 0;
        if (!ReadRequiredInteger(
                effect,
                {"province_id", "provinceid", "id"},
                provinceId,
                "province_id_invalid",
                error
            ))
        {
            return false;
        }
        std::uintptr_t targetCountry = 0;
        CountryTagValue targetTag{};
        if (!ResolveRequiredCountryByTag(
                executableBase,
                effect,
                setOwner
                    ? std::initializer_list<std::string_view>{
                        "owner", "owner_tag", "target_tag", "country", "tag"
                    }
                    : std::initializer_list<std::string_view>{
                        "controller", "controller_tag", "target_tag",
                        "country", "tag"
                    },
                targetCountry,
                targetTag,
                setOwner
                    ? "province_owner_tag_invalid"
                    : "province_controller_tag_invalid",
                error,
                context.safetyLease
            ))
        {
            return false;
        }
        (void)targetCountry;

        std::uintptr_t gameState = 0;
        std::uintptr_t province = 0;
        if (!ResolveGameState(executableBase, gameState))
        {
            error = "hoi3_game_state_unavailable";
            return false;
        }
        if (!ResolveProvince(
                gameState,
                provinceId,
                province,
                error,
                context.safetyLease
            ))
        {
            return false;
        }
        CountryTagValue oldOwner{};
        CountryTagValue oldController{};
        if (!ReadProvinceCountry(
                province,
                ProvinceOwnerOffset,
                oldOwner
            )
            || !ReadProvinceCountry(
                province,
                ProvinceControllerOffset,
                oldController
            ))
        {
            error = "hoi3_province_country_state_unavailable";
            return false;
        }

        const auto state = std::make_shared<ApplyState>();
        std::uint8_t* const base = executableBase;
        const uint32_t nativeProvinceId = static_cast<uint32_t>(provinceId);
        prepared.apply = [
            base,
            gameState,
            province,
            nativeProvinceId,
            oldOwner,
            oldController,
            targetTag,
            setOwner,
            state
        ](std::string& applyError)
        {
            CountryTagValue currentOwner{};
            CountryTagValue currentController{};
            if (!ReadProvinceCountry(
                    province,
                    ProvinceOwnerOffset,
                    currentOwner
                )
                || !ReadProvinceCountry(
                    province,
                    ProvinceControllerOffset,
                    currentController
                )
                || !CountryTagsEqual(currentOwner, oldOwner)
                || !CountryTagsEqual(currentController, oldController))
            {
                applyError = "province_country_changed_before_apply";
                return false;
            }
            const bool changed = setOwner
                ? !CountryTagsEqual(oldOwner, targetTag)
                : !CountryTagsEqual(oldController, targetTag);
            const bool invoked = !changed
                || (setOwner
                    ? InvokeProvinceOwnerSetter(
                        base,
                        gameState,
                        province,
                        targetTag
                    )
                    : InvokeChangeControllerEffect(
                        base,
                        nativeProvinceId,
                        targetTag
                    ));
            state->applied = changed && invoked;
            CountryTagValue current{};
            if (!invoked
                || !ReadProvinceCountry(
                    province,
                    setOwner ? ProvinceOwnerOffset : ProvinceControllerOffset,
                    current
                )
                || !CountryTagsEqual(current, targetTag))
            {
                applyError = setOwner
                    ? "province_owner_native_apply_failed"
                    : "province_controller_native_apply_failed";
                return false;
            }
            applyError.clear();
            return true;
        };
        prepared.rollback = [
            base,
            gameState,
            province,
            nativeProvinceId,
            oldOwner,
            oldController,
            setOwner,
            state
        ]
        {
            if (!state->applied)
            {
                return;
            }
            if (setOwner)
            {
                InvokeProvinceOwnerSetter(
                    base,
                    gameState,
                    province,
                    oldOwner
                );
                InvokeChangeControllerEffect(
                    base,
                    nativeProvinceId,
                    oldController
                );
            }
            else
            {
                InvokeChangeControllerEffect(
                    base,
                    nativeProvinceId,
                    oldController
                );
            }
            state->applied = false;
        };
        error.clear();
        return true;
    }

    bool PrepareProvinceCore(
        const NativeEffect& effect,
        const NativeEffectExecutionContext& context,
        PreparedNativeEffect& prepared,
        std::string& error,
        bool addCore
    ) const
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        int64_t provinceId = 0;
        if (!ReadRequiredInteger(
                effect,
                {"province_id", "provinceid", "id"},
                provinceId,
                "province_id_invalid",
                error
            ))
        {
            return false;
        }
        std::uintptr_t targetCountry = 0;
        CountryTagValue targetTag{};
        if (!ResolveRequiredCountryByTag(
                executableBase,
                effect,
                {"core", "core_tag", "target_tag", "country", "tag"},
                targetCountry,
                targetTag,
                "province_core_tag_invalid",
                error,
                context.safetyLease
            ))
        {
            return false;
        }
        (void)targetCountry;

        std::uintptr_t gameState = 0;
        std::uintptr_t province = 0;
        if (!ResolveGameState(executableBase, gameState))
        {
            error = "hoi3_game_state_unavailable";
            return false;
        }
        if (!ResolveProvince(
                gameState,
                provinceId,
                province,
                error,
                context.safetyLease
            ))
        {
            return false;
        }
        bool oldFound = false;
        if (!FindProvinceCore(province, targetTag.index, oldFound))
        {
            error = "hoi3_province_core_list_unavailable";
            return false;
        }
        if (oldFound == addCore)
        {
            error = addCore
                ? "province_core_already_present"
                : "province_core_missing";
            return false;
        }

        const auto state = std::make_shared<ApplyState>();
        std::uint8_t* const base = executableBase;
        const uint32_t nativeProvinceId = static_cast<uint32_t>(provinceId);
        prepared.apply = [
            base,
            province,
            nativeProvinceId,
            targetTag,
            oldFound,
            addCore,
            state
        ](std::string& applyError)
        {
            bool currentFound = false;
            if (!FindProvinceCore(
                    province,
                    targetTag.index,
                    currentFound
                )
                || currentFound != oldFound)
            {
                applyError = "province_core_changed_before_apply";
                return false;
            }
            const bool invoked = InvokeProvinceCoreEffect(
                base,
                nativeProvinceId,
                targetTag,
                addCore
            );
            state->applied = invoked;
            if (!invoked
                || !FindProvinceCore(
                    province,
                    targetTag.index,
                    currentFound
                )
                || currentFound != addCore)
            {
                applyError = addCore
                    ? "province_core_native_add_failed"
                    : "province_core_native_remove_failed";
                return false;
            }
            applyError.clear();
            return true;
        };
        prepared.rollback = [
            base,
            nativeProvinceId,
            targetTag,
            addCore,
            state
        ]
        {
            if (state->applied)
            {
                InvokeProvinceCoreEffect(
                    base,
                    nativeProvinceId,
                    targetTag,
                    !addCore
                );
            }
            state->applied = false;
        };
        error.clear();
        return true;
    }

    bool PrepareProvinceBuildingLevel(
        const NativeEffect& effect,
        const NativeEffectExecutionContext& context,
        PreparedNativeEffect& prepared,
        std::string& error
    ) const
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        int64_t provinceId = 0;
        int64_t requestedLevel = 0;
        std::string buildingName;
        if (!ReadRequiredInteger(
                effect,
                {"province_id", "provinceid", "id"},
                provinceId,
                "province_id_invalid",
                error
            )
            || !ReadRequiredString(
                effect,
                {"building", "building_name", "name"},
                buildingName,
                "building_name_invalid",
                error
            )
            || !ReadRequiredInteger(
                effect,
                {"level", "value", "target"},
                requestedLevel,
                "building_level_invalid",
                error
            ))
        {
            return false;
        }
        if (requestedLevel < 0 || requestedLevel > MaximumBuildingLevel)
        {
            error = "building_level_out_of_range";
            return false;
        }

        std::uintptr_t gameState = 0;
        std::uintptr_t province = 0;
        if (!ResolveGameState(executableBase, gameState))
        {
            error = "hoi3_game_state_unavailable";
            return false;
        }
        if (!ResolveProvince(
                gameState,
                provinceId,
                province,
                error,
                context.safetyLease
            ))
        {
            return false;
        }
        void* const definitionPointer = FindBuildingDefinition(
            executableBase,
            std::move(buildingName),
            error
        );
        if (!definitionPointer)
        {
            return false;
        }
        const std::uintptr_t definition = reinterpret_cast<std::uintptr_t>(
            definitionPointer
        );
        std::uintptr_t record = 0;
        if (!ResolveBuildingRecord(province, definition, record, error))
        {
            return false;
        }
        int32_t oldCompleted = 0;
        int32_t oldMaximum = 0;
        if (!TryRead(
                record + BuildingRecordCompletedLevelOffset,
                oldCompleted
            )
            || !TryRead(
                record + BuildingRecordMaximumLevelOffset,
                oldMaximum
            ))
        {
            error = "hoi3_province_building_level_unavailable";
            return false;
        }
        const int32_t expected = static_cast<int32_t>(requestedLevel)
            * FixedPointScale;
        const auto state = std::make_shared<ApplyState>();
        std::uint8_t* const base = executableBase;
        const uint32_t nativeProvinceId = static_cast<uint32_t>(provinceId);
        prepared.apply = [
            base,
            province,
            nativeProvinceId,
            definition,
            record,
            oldCompleted,
            oldMaximum,
            expected,
            requestedLevel,
            state
        ](std::string& applyError)
        {
            int32_t currentCompleted = 0;
            int32_t currentMaximum = 0;
            if (!TryRead(
                    record + BuildingRecordCompletedLevelOffset,
                    currentCompleted
                )
                || !TryRead(
                    record + BuildingRecordMaximumLevelOffset,
                    currentMaximum
                )
                || currentCompleted != oldCompleted
                || currentMaximum != oldMaximum)
            {
                applyError = "province_building_changed_before_apply";
                return false;
            }
            const bool changed = oldCompleted != expected
                || oldMaximum != expected;
            const bool invoked = !changed || InvokeBuildingLevelCommand(
                base,
                nativeProvinceId,
                definition,
                static_cast<int32_t>(requestedLevel)
            );
            state->applied = changed && invoked;
            if (!invoked
                || !TryRead(
                    record + BuildingRecordCompletedLevelOffset,
                    currentCompleted
                )
                || !TryRead(
                    record + BuildingRecordMaximumLevelOffset,
                    currentMaximum
                )
                || currentCompleted != expected
                || currentMaximum != expected)
            {
                applyError = "province_building_native_apply_failed";
                return false;
            }
            applyError.clear();
            return true;
        };
        prepared.rollback = [
            base,
            province,
            record,
            oldCompleted,
            oldMaximum,
            state
        ]
        {
            if (state->applied)
            {
                RestoreBuildingLevels(
                    base,
                    province,
                    record,
                    oldCompleted,
                    oldMaximum
                );
            }
            state->applied = false;
        };
        error.clear();
        return true;
    }

    bool PrepareTechnologyLevel(
        const NativeEffect& effect,
        const NativeEffectExecutionContext& context,
        PreparedNativeEffect& prepared,
        std::string& error
    ) const
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        std::string technologyName;
        int64_t requestedLevel = 0;
        if (!ReadRequiredString(
                effect,
                {"technology", "technology_name", "tech", "name"},
                technologyName,
                "technology_name_invalid",
                error
            )
            || !ReadRequiredInteger(
                effect,
                {"level", "value", "target"},
                requestedLevel,
                "technology_level_invalid",
                error
            ))
        {
            return false;
        }

        std::uintptr_t gameState = 0;
        std::uintptr_t country = 0;
        uint32_t countryIndex = 0;
        if (!ResolvePlayerCountryTarget(
                executableBase,
                effect,
                context,
                gameState,
                country,
                countryIndex,
                error
            ))
        {
            return false;
        }
        (void)gameState;
        CountryTagValue countryTag{};
        if (!ReadCountryTagValue(country, countryTag)
            || countryTag.index != countryIndex)
        {
            error = "hoi3_player_country_tag_unavailable";
            return false;
        }
        void* const definitionPointer = FindTechnologyDefinition(
            executableBase,
            std::move(technologyName),
            error,
            context.safetyLease
        );
        if (!definitionPointer)
        {
            return false;
        }
        const std::uintptr_t definition = reinterpret_cast<std::uintptr_t>(
            definitionPointer
        );
        TechnologyStateEntry entry;
        if (!ResolveTechnologyStateEntry(
                country,
                definition,
                entry,
                error
            ))
        {
            return false;
        }
        if (requestedLevel < 0
            || requestedLevel > entry.maximumLevel)
        {
            error = "technology_level_out_of_range";
            return false;
        }
        const int32_t expected = static_cast<int32_t>(requestedLevel);
        const int32_t oldLevel = entry.level;
        const uint64_t oldProgress = entry.progress;
        const std::uintptr_t levelField = entry.levelField;
        const std::uintptr_t progressField = entry.progressField;
        const auto state = std::make_shared<ApplyState>();
        std::uint8_t* const base = executableBase;
        prepared.apply = [
            base,
            countryTag,
            definition,
            levelField,
            progressField,
            oldLevel,
            oldProgress,
            expected,
            state
        ](std::string& applyError)
        {
            int32_t currentLevel = 0;
            uint64_t currentProgress = 0;
            if (!TryRead(levelField, currentLevel)
                || !TryRead(progressField, currentProgress)
                || currentLevel != oldLevel
                || currentProgress != oldProgress)
            {
                applyError = "technology_state_changed_before_apply";
                return false;
            }
            const bool changed = oldLevel != expected || oldProgress != 0;
            const bool invoked = !changed || InvokeTechnologyLevelCommand(
                base,
                countryTag,
                definition,
                expected
            );
            state->applied = changed && invoked;
            if (!invoked
                || !TryRead(levelField, currentLevel)
                || !TryRead(progressField, currentProgress)
                || currentLevel != expected
                || currentProgress != 0)
            {
                applyError = "technology_level_native_apply_failed";
                return false;
            }
            applyError.clear();
            return true;
        };
        prepared.rollback = [
            base,
            countryTag,
            definition,
            oldLevel,
            oldProgress,
            state
        ]
        {
            if (!state->applied)
            {
                return;
            }
            InvokeTechnologyLevelCommand(
                base,
                countryTag,
                definition,
                oldLevel
            );
            InvokeResearchProgressCommand(
                base,
                countryTag,
                definition,
                oldProgress
            );
            state->applied = false;
        };
        error.clear();
        return true;
    }

    bool PrepareResearchProgress(
        const NativeEffect& effect,
        const NativeEffectExecutionContext& context,
        PreparedNativeEffect& prepared,
        std::string& error
    ) const
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        std::string technologyName;
        double requestedProgress = 0;
        if (!ReadRequiredString(
                effect,
                {"technology", "technology_name", "tech", "name"},
                technologyName,
                "technology_name_invalid",
                error
            )
            || !ReadRequiredNumber(
                effect,
                {"progress", "value", "target"},
                requestedProgress,
                "research_progress_invalid",
                error
            ))
        {
            return false;
        }
        if (requestedProgress < 0.0 || requestedProgress >= 1.0)
        {
            error = "research_progress_out_of_range";
            return false;
        }
        const uint64_t requestedRaw = std::min<uint64_t>(
            static_cast<uint64_t>(std::llround(
                requestedProgress * ResearchCompletionThreshold
            )),
            ResearchCompletionThreshold - 1
        );

        std::uintptr_t gameState = 0;
        std::uintptr_t country = 0;
        uint32_t countryIndex = 0;
        if (!ResolvePlayerCountryTarget(
                executableBase,
                effect,
                context,
                gameState,
                country,
                countryIndex,
                error
            ))
        {
            return false;
        }
        (void)gameState;
        CountryTagValue countryTag{};
        if (!ReadCountryTagValue(country, countryTag)
            || countryTag.index != countryIndex)
        {
            error = "hoi3_player_country_tag_unavailable";
            return false;
        }
        void* const definitionPointer = FindTechnologyDefinition(
            executableBase,
            std::move(technologyName),
            error,
            context.safetyLease
        );
        if (!definitionPointer)
        {
            return false;
        }
        const std::uintptr_t definition = reinterpret_cast<std::uintptr_t>(
            definitionPointer
        );
        TechnologyStateEntry entry;
        if (!ResolveTechnologyStateEntry(
                country,
                definition,
                entry,
                error
            ))
        {
            return false;
        }
        bool active = false;
        if (!FindResearchEntry(country, definition, active))
        {
            error = "hoi3_research_list_unavailable";
            return false;
        }
        if (!active)
        {
            error = "research_not_active";
            return false;
        }
        const uint64_t oldProgress = entry.progress;
        const std::uintptr_t progressField = entry.progressField;
        const auto state = std::make_shared<ApplyState>();
        std::uint8_t* const base = executableBase;
        prepared.apply = [
            base,
            country,
            countryTag,
            definition,
            progressField,
            oldProgress,
            requestedRaw,
            state
        ](std::string& applyError)
        {
            uint64_t currentProgress = 0;
            bool currentActive = false;
            if (!TryRead(progressField, currentProgress)
                || currentProgress != oldProgress
                || !FindResearchEntry(
                    country,
                    definition,
                    currentActive
                )
                || !currentActive)
            {
                applyError = "research_state_changed_before_apply";
                return false;
            }
            const bool changed = oldProgress != requestedRaw;
            const bool invoked = !changed || InvokeResearchProgressCommand(
                base,
                countryTag,
                definition,
                requestedRaw
            );
            state->applied = changed && invoked;
            if (!invoked
                || !TryRead(progressField, currentProgress)
                || currentProgress != requestedRaw)
            {
                applyError = "research_progress_native_apply_failed";
                return false;
            }
            applyError.clear();
            return true;
        };
        prepared.rollback = [
            base,
            countryTag,
            definition,
            oldProgress,
            state
        ]
        {
            if (state->applied)
            {
                InvokeResearchProgressCommand(
                    base,
                    countryTag,
                    definition,
                    oldProgress
                );
            }
            state->applied = false;
        };
        error.clear();
        return true;
    }

    bool PrepareResearchCancel(
        const NativeEffect& effect,
        const NativeEffectExecutionContext& context,
        PreparedNativeEffect& prepared,
        std::string& error
    ) const
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        std::string technologyName;
        if (!ReadRequiredString(
                effect,
                {"technology", "technology_name", "tech", "name"},
                technologyName,
                "technology_name_invalid",
                error
            ))
        {
            return false;
        }
        std::uintptr_t gameState = 0;
        std::uintptr_t country = 0;
        uint32_t countryIndex = 0;
        if (!ResolvePlayerCountryTarget(
                executableBase,
                effect,
                context,
                gameState,
                country,
                countryIndex,
                error
            ))
        {
            return false;
        }
        (void)gameState;
        CountryTagValue countryTag{};
        if (!ReadCountryTagValue(country, countryTag)
            || countryTag.index != countryIndex)
        {
            error = "hoi3_player_country_tag_unavailable";
            return false;
        }
        void* const definitionPointer = FindTechnologyDefinition(
            executableBase,
            std::move(technologyName),
            error,
            context.safetyLease
        );
        if (!definitionPointer)
        {
            return false;
        }
        const std::uintptr_t definition = reinterpret_cast<std::uintptr_t>(
            definitionPointer
        );
        TechnologyStateEntry entry;
        if (!ResolveTechnologyStateEntry(
                country,
                definition,
                entry,
                error
            ))
        {
            return false;
        }
        bool oldActive = false;
        if (!FindResearchEntry(country, definition, oldActive))
        {
            error = "hoi3_research_list_unavailable";
            return false;
        }
        if (!oldActive)
        {
            error = "research_not_active";
            return false;
        }
        const uint64_t oldProgress = entry.progress;
        const auto state = std::make_shared<ApplyState>();
        std::uint8_t* const base = executableBase;
        prepared.apply = [
            base,
            country,
            countryTag,
            definition,
            state
        ](std::string& applyError)
        {
            bool currentActive = false;
            if (!FindResearchEntry(country, definition, currentActive)
                || !currentActive)
            {
                applyError = "research_state_changed_before_apply";
                return false;
            }
            const bool invoked = InvokeResearchListCommand(
                base,
                Profile.stopResearchCommandExecute,
                countryTag,
                definition
            );
            state->applied = invoked;
            if (!invoked
                || !FindResearchEntry(
                    country,
                    definition,
                    currentActive
                )
                || currentActive)
            {
                applyError = "research_cancel_native_apply_failed";
                return false;
            }
            applyError.clear();
            return true;
        };
        prepared.rollback = [
            base,
            countryTag,
            definition,
            oldProgress,
            state
        ]
        {
            if (!state->applied)
            {
                return;
            }
            InvokeResearchListCommand(
                base,
                Profile.startResearchCommandExecute,
                countryTag,
                definition
            );
            InvokeResearchProgressCommand(
                base,
                countryTag,
                definition,
                oldProgress
            );
            state->applied = false;
        };
        error.clear();
        return true;
    }

    bool PrepareResearchComplete(
        const NativeEffect& effect,
        const NativeEffectExecutionContext& context,
        PreparedNativeEffect& prepared,
        std::string& error
    ) const
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        std::string technologyName;
        if (!ReadRequiredString(
                effect,
                {"technology", "technology_name", "tech", "name"},
                technologyName,
                "technology_name_invalid",
                error
            ))
        {
            return false;
        }
        std::uintptr_t gameState = 0;
        std::uintptr_t country = 0;
        uint32_t countryIndex = 0;
        if (!ResolvePlayerCountryTarget(
                executableBase,
                effect,
                context,
                gameState,
                country,
                countryIndex,
                error
            ))
        {
            return false;
        }
        (void)gameState;
        CountryTagValue countryTag{};
        if (!ReadCountryTagValue(country, countryTag)
            || countryTag.index != countryIndex)
        {
            error = "hoi3_player_country_tag_unavailable";
            return false;
        }
        void* const definitionPointer = FindTechnologyDefinition(
            executableBase,
            std::move(technologyName),
            error,
            context.safetyLease
        );
        if (!definitionPointer)
        {
            return false;
        }
        const std::uintptr_t definition = reinterpret_cast<std::uintptr_t>(
            definitionPointer
        );
        TechnologyStateEntry entry;
        if (!ResolveTechnologyStateEntry(
                country,
                definition,
                entry,
                error
            ))
        {
            return false;
        }
        bool active = false;
        if (!FindResearchEntry(country, definition, active))
        {
            error = "hoi3_research_list_unavailable";
            return false;
        }
        if (!active)
        {
            error = "research_not_active";
            return false;
        }
        if (entry.level >= entry.maximumLevel)
        {
            error = "technology_already_at_maximum_level";
            return false;
        }
        const int32_t oldLevel = entry.level;
        const int32_t expectedLevel = oldLevel + 1;
        const uint64_t oldProgress = entry.progress;
        const std::uintptr_t levelField = entry.levelField;
        const std::uintptr_t progressField = entry.progressField;
        const auto state = std::make_shared<ApplyState>();
        std::uint8_t* const base = executableBase;
        prepared.apply = [
            base,
            country,
            countryIndex,
            countryTag,
            definition,
            levelField,
            progressField,
            oldLevel,
            oldProgress,
            expectedLevel,
            state
        ](std::string& applyError)
        {
            int32_t currentLevel = 0;
            uint64_t currentProgress = 0;
            bool currentActive = false;
            if (!TryRead(levelField, currentLevel)
                || !TryRead(progressField, currentProgress)
                || currentLevel != oldLevel
                || currentProgress != oldProgress
                || !FindResearchEntry(
                    country,
                    definition,
                    currentActive
                )
                || !currentActive)
            {
                applyError = "research_state_changed_before_apply";
                return false;
            }
            if (!InvokeResearchProgressCommand(
                    base,
                    countryTag,
                    definition,
                    ResearchCompletionThreshold
                ))
            {
                applyError = "research_completion_progress_set_failed";
                return false;
            }
            state->applied = true;
            if (!InvokeTechnologyInvestmentEffect(
                    base,
                    countryIndex,
                    definition,
                    0
                )
                || !InvokeResearchListCommand(
                    base,
                    Profile.stopResearchCommandExecute,
                    countryTag,
                    definition
                )
                || !TryRead(levelField, currentLevel)
                || currentLevel != expectedLevel
                || !FindResearchEntry(
                    country,
                    definition,
                    currentActive
                )
                || currentActive)
            {
                applyError = "research_complete_native_apply_failed";
                return false;
            }
            applyError.clear();
            return true;
        };
        prepared.rollback = [
            base,
            countryTag,
            definition,
            oldLevel,
            oldProgress,
            state
        ]
        {
            if (!state->applied)
            {
                return;
            }
            InvokeTechnologyLevelCommand(
                base,
                countryTag,
                definition,
                oldLevel
            );
            InvokeResearchListCommand(
                base,
                Profile.startResearchCommandExecute,
                countryTag,
                definition
            );
            InvokeResearchProgressCommand(
                base,
                countryTag,
                definition,
                oldProgress
            );
            state->applied = false;
        };
        error.clear();
        return true;
    }

    bool PrepareCountryCapital(
        const NativeEffect& effect,
        const NativeEffectExecutionContext& context,
        PreparedNativeEffect& prepared,
        std::string& error,
        bool actingOnly
    ) const
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        int64_t provinceId = 0;
        if (!ReadRequiredInteger(
                effect,
                {"province_id", "provinceid", "capital", "id"},
                provinceId,
                "capital_province_id_invalid",
                error
            ))
        {
            return false;
        }
        std::uintptr_t gameState = 0;
        std::uintptr_t country = 0;
        uint32_t countryIndex = 0;
        if (!ResolvePlayerCountryTarget(
                executableBase,
                effect,
                context,
                gameState,
                country,
                countryIndex,
                error
            ))
        {
            return false;
        }
        CountryTagValue countryTag{};
        std::uintptr_t province = 0;
        CountryTagValue provinceOwner{};
        if (!ReadCountryTagValue(country, countryTag)
            || countryTag.index != countryIndex
            || !ResolveProvince(
                gameState,
                provinceId,
                province,
                error,
                context.safetyLease
            )
            || !ReadProvinceCountry(
                province,
                ProvinceOwnerOffset,
                provinceOwner
            ))
        {
            if (error.empty())
            {
                error = "hoi3_capital_state_unavailable";
            }
            return false;
        }
        if (!CountryTagsEqual(countryTag, provinceOwner))
        {
            error = "capital_province_not_owned";
            return false;
        }
        uint32_t oldOfficial = 0;
        uint32_t oldActing = 0;
        if (!TryRead(
                country + CountryCapitalProvinceOffset,
                oldOfficial
            )
            || !TryRead(
                country + CountryActingCapitalProvinceOffset,
                oldActing
            ))
        {
            error = "hoi3_country_capital_unavailable";
            return false;
        }
        const uint32_t expected = static_cast<uint32_t>(provinceId);
        const auto state = std::make_shared<ApplyState>();
        std::uint8_t* const base = executableBase;
        prepared.apply = [
            base,
            country,
            countryIndex,
            oldOfficial,
            oldActing,
            expected,
            actingOnly,
            state
        ](std::string& applyError)
        {
            uint32_t currentOfficial = 0;
            uint32_t currentActing = 0;
            if (!TryRead(
                    country + CountryCapitalProvinceOffset,
                    currentOfficial
                )
                || !TryRead(
                    country + CountryActingCapitalProvinceOffset,
                    currentActing
                )
                || currentOfficial != oldOfficial
                || currentActing != oldActing)
            {
                applyError = "country_capital_changed_before_apply";
                return false;
            }
            const bool changed = actingOnly
                ? oldActing != expected
                : oldOfficial != expected || oldActing != expected;
            const bool invoked = !changed
                || (actingOnly
                    ? InvokeActingCapitalSetter(base, country, expected)
                    : InvokeCapitalEffect(base, countryIndex, expected));
            state->applied = changed && invoked;
            if (!invoked
                || !TryRead(
                    country + CountryCapitalProvinceOffset,
                    currentOfficial
                )
                || !TryRead(
                    country + CountryActingCapitalProvinceOffset,
                    currentActing
                )
                || currentActing != expected
                || (!actingOnly && currentOfficial != expected)
                || (actingOnly && currentOfficial != oldOfficial))
            {
                applyError = actingOnly
                    ? "country_acting_capital_native_apply_failed"
                    : "country_capital_native_apply_failed";
                return false;
            }
            applyError.clear();
            return true;
        };
        prepared.rollback = [
            base,
            country,
            countryIndex,
            oldOfficial,
            oldActing,
            actingOnly,
            state
        ]
        {
            if (!state->applied)
            {
                return;
            }
            if (actingOnly)
            {
                InvokeActingCapitalSetter(base, country, oldActing);
            }
            else
            {
                InvokeCapitalEffect(base, countryIndex, oldOfficial);
                if (oldActing != oldOfficial)
                {
                    InvokeActingCapitalSetter(base, country, oldActing);
                }
            }
            state->applied = false;
        };
        error.clear();
        return true;
    }

    bool PrepareCountryScalar(
        const NativeEffect& effect,
        const NativeEffectExecutionContext& context,
        PreparedNativeEffect& prepared,
        std::string& error,
        CountryScalarKind kind,
        bool setValue
    ) const
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        const CountryScalarSpec spec = GetCountryScalarSpec(kind);
        const std::string prefix = std::string("country_") + spec.name;
        double requested = 0;
        const bool hasValue = setValue
            ? ReadRequiredNumber(
                effect,
                {"value", "target", "amount"},
                requested,
                prefix + "_value_invalid",
                error
            )
            : ReadRequiredNumber(
                effect,
                {"amount", "delta"},
                requested,
                prefix + "_amount_invalid",
                error
            );
        if (!hasValue)
        {
            return false;
        }
        int32_t requestedFixed = 0;
        if (!ScaleFixedPoint(
                requested,
                requestedFixed,
                setValue ? prefix + "_value" : prefix + "_amount",
                error
            ))
        {
            return false;
        }
        if (!setValue && requestedFixed == 0)
        {
            error = prefix + "_amount_below_precision";
            return false;
        }

        std::uintptr_t gameState = 0;
        std::uintptr_t country = 0;
        uint32_t countryIndex = 0;
        if (!ResolvePlayerCountryTarget(
                executableBase,
                effect,
                context,
                gameState,
                country,
                countryIndex,
                error
            ))
        {
            return false;
        }

        int32_t oldValue = 0;
        if (!TryRead(country + spec.fieldOffset, oldValue))
        {
            error = "hoi3_" + prefix + "_unavailable";
            return false;
        }
        const int64_t expected64 = setValue
            ? requestedFixed
            : static_cast<int64_t>(oldValue) + requestedFixed;
        if (expected64 < spec.minimum || expected64 > spec.maximum)
        {
            error = prefix + "_result_out_of_range";
            return false;
        }
        const int32_t expected = static_cast<int32_t>(expected64);
        const int64_t delta64 = static_cast<int64_t>(expected) - oldValue;
        if (delta64 < std::numeric_limits<int32_t>::min()
            || delta64 > std::numeric_limits<int32_t>::max())
        {
            error = prefix + "_delta_out_of_range";
            return false;
        }
        const int32_t delta = static_cast<int32_t>(delta64);
        const auto state = std::make_shared<ApplyState>();
        std::uint8_t* const base = executableBase;
        const std::uintptr_t fieldOffset = spec.fieldOffset;
        const std::uintptr_t executeRva = spec.executeRva;

        prepared.apply = [
            base,
            country,
            countryIndex,
            fieldOffset,
            executeRva,
            delta,
            oldValue,
            expected,
            prefix,
            state
        ](std::string& applyError)
        {
            int32_t current = 0;
            if (!TryRead(country + fieldOffset, current)
                || current != oldValue)
            {
                applyError = prefix + "_changed_before_apply";
                return false;
            }
            if ((delta != 0 && !InvokeCountryValueEffect(
                    base,
                    executeRva,
                    countryIndex,
                    delta
                ))
                || !TryRead(country + fieldOffset, current)
                || current != expected)
            {
                applyError = prefix + "_native_apply_failed";
                return false;
            }
            state->applied = true;
            applyError.clear();
            return true;
        };
        prepared.rollback = [
            base,
            country,
            countryIndex,
            fieldOffset,
            executeRva,
            oldValue,
            state
        ]
        {
            if (!state->applied)
            {
                return;
            }
            int32_t current = 0;
            if (!TryRead(country + fieldOffset, current))
            {
                return;
            }
            const int64_t rollback64 = static_cast<int64_t>(oldValue)
                - current;
            if (rollback64 < std::numeric_limits<int32_t>::min()
                || rollback64 > std::numeric_limits<int32_t>::max())
            {
                return;
            }
            if (rollback64 != 0)
            {
                InvokeCountryValueEffect(
                    base,
                    executeRva,
                    countryIndex,
                    static_cast<int32_t>(rollback64)
                );
            }
            state->applied = false;
        };
        error.clear();
        return true;
    }

    bool PrepareDirectCountryScalar(
        const NativeEffect& effect,
        const NativeEffectExecutionContext& context,
        PreparedNativeEffect& prepared,
        std::string& error,
        DirectCountryScalarKind kind,
        bool setValue
    ) const
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        const DirectCountryScalarSpec spec = GetDirectCountryScalarSpec(kind);
        const std::string prefix = std::string("country_") + spec.name;
        int32_t requestedValue = 0;
        if (spec.integerOnly)
        {
            int64_t requested = 0;
            if (!ReadRequiredInteger(
                    effect,
                    setValue
                        ? std::initializer_list<std::string_view>{
                            "value", "target", "amount"
                        }
                        : std::initializer_list<std::string_view>{
                            "amount", "delta"
                        },
                    requested,
                    setValue
                        ? prefix + "_value_invalid"
                        : prefix + "_amount_invalid",
                    error
                )
                || requested < std::numeric_limits<int32_t>::min()
                || requested > std::numeric_limits<int32_t>::max())
            {
                if (error.empty())
                {
                    error = prefix + "_value_out_of_range";
                }
                return false;
            }
            requestedValue = static_cast<int32_t>(requested);
        }
        else
        {
            double requested = 0;
            if (!ReadRequiredNumber(
                    effect,
                    setValue
                        ? std::initializer_list<std::string_view>{
                            "value", "target", "amount"
                        }
                        : std::initializer_list<std::string_view>{
                            "amount", "delta"
                        },
                    requested,
                    setValue
                        ? prefix + "_value_invalid"
                        : prefix + "_amount_invalid",
                    error
                )
                || !ScaleFixedPoint(
                    requested,
                    requestedValue,
                    setValue ? prefix + "_value" : prefix + "_amount",
                    error
                ))
            {
                return false;
            }
        }
        if (!setValue && requestedValue == 0)
        {
            error = prefix + "_amount_below_precision";
            return false;
        }

        std::uintptr_t gameState = 0;
        std::uintptr_t country = 0;
        uint32_t countryIndex = 0;
        if (!ResolvePlayerCountryTarget(
                executableBase,
                effect,
                context,
                gameState,
                country,
                countryIndex,
                error
            ))
        {
            return false;
        }
        const std::uintptr_t field = country + spec.fieldOffset;
        int32_t oldValue = 0;
        if (!TryRead(field, oldValue))
        {
            error = "hoi3_" + prefix + "_unavailable";
            return false;
        }
        const int64_t expected64 = setValue
            ? requestedValue
            : static_cast<int64_t>(oldValue) + requestedValue;
        if (expected64 < spec.minimum || expected64 > spec.maximum)
        {
            error = prefix + "_result_out_of_range";
            return false;
        }
        const int32_t expected = static_cast<int32_t>(expected64);
        const auto state = std::make_shared<ApplyState>();
        prepared.apply = [
            field,
            oldValue,
            expected,
            prefix,
            state
        ](std::string& applyError)
        {
            int32_t current = 0;
            if (!TryRead(field, current) || current != oldValue)
            {
                applyError = prefix + "_changed_before_apply";
                return false;
            }
            state->applied = true;
            if (!TryWrite(field, expected)
                || !TryRead(field, current)
                || current != expected)
            {
                applyError = prefix + "_native_apply_failed";
                return false;
            }
            applyError.clear();
            return true;
        };
        prepared.rollback = [field, oldValue, state]
        {
            if (!state->applied)
            {
                return;
            }
            TryWrite(field, oldValue);
            state->applied = false;
        };
        error.clear();
        return true;
    }

    bool PrepareDiplomaticInfluence(
        const NativeEffect& effect,
        const NativeEffectExecutionContext& context,
        PreparedNativeEffect& prepared,
        std::string& error,
        bool setValue
    ) const
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        double requested = 0;
        if (!ReadRequiredNumber(
                effect,
                setValue
                    ? std::initializer_list<std::string_view>{
                        "value", "target", "amount"
                    }
                    : std::initializer_list<std::string_view>{
                        "amount", "delta"
                    },
                requested,
                setValue
                    ? "country_diplomatic_influence_value_invalid"
                    : "country_diplomatic_influence_amount_invalid",
                error
            ))
        {
            return false;
        }
        int32_t requestedFixed = 0;
        if (!ScaleFixedPoint(
                requested,
                requestedFixed,
                setValue
                    ? "country_diplomatic_influence_value"
                    : "country_diplomatic_influence_amount",
                error
            )
            || (!setValue && requestedFixed == 0))
        {
            if (error.empty())
            {
                error = "country_diplomatic_influence_amount_below_precision";
            }
            return false;
        }

        std::uintptr_t gameState = 0;
        std::uintptr_t country = 0;
        uint32_t countryIndex = 0;
        if (!ResolvePlayerCountryTarget(
                executableBase,
                effect,
                context,
                gameState,
                country,
                countryIndex,
                error
            ))
        {
            return false;
        }
        const std::uintptr_t field = country
            + CountryDiplomaticInfluenceOffset;
        int32_t oldValue = 0;
        if (!TryRead(field, oldValue))
        {
            error = "hoi3_country_diplomatic_influence_unavailable";
            return false;
        }
        const int64_t expected64 = setValue
            ? requestedFixed
            : static_cast<int64_t>(oldValue) + requestedFixed;
        if (expected64 < 0
            || expected64 > std::numeric_limits<int32_t>::max())
        {
            error = "country_diplomatic_influence_result_out_of_range";
            return false;
        }
        const int32_t expected = static_cast<int32_t>(expected64);
        std::uint8_t* const base = executableBase;
        const auto state = std::make_shared<ApplyState>();
        prepared.apply = [
            base,
            country,
            field,
            oldValue,
            expected,
            state
        ](std::string& applyError)
        {
            int32_t current = 0;
            if (!TryRead(field, current) || current != oldValue)
            {
                applyError = "country_diplomatic_influence_changed_before_apply";
                return false;
            }
            state->applied = true;
            const int32_t delta = expected - oldValue;
            if ((delta != 0
                    && !InvokeDiplomaticInfluenceAdd(base, country, delta))
                || !TryRead(field, current)
                || current != expected)
            {
                applyError = "country_diplomatic_influence_native_apply_failed";
                return false;
            }
            applyError.clear();
            return true;
        };
        prepared.rollback = [base, country, field, oldValue, state]
        {
            if (!state->applied)
            {
                return;
            }
            int32_t current = 0;
            if (TryRead(field, current) && current != oldValue)
            {
                InvokeDiplomaticInfluenceAdd(
                    base,
                    country,
                    oldValue - current
                );
            }
            state->applied = false;
        };
        error.clear();
        return true;
    }

    bool PrepareLeadership(
        const NativeEffect& effect,
        const NativeEffectExecutionContext& context,
        PreparedNativeEffect& prepared,
        std::string& error,
        bool setValue
    ) const
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        double requested = 0;
        if (!ReadRequiredNumber(
                effect,
                setValue
                    ? std::initializer_list<std::string_view>{
                        "value", "target", "amount"
                    }
                    : std::initializer_list<std::string_view>{
                        "amount", "delta"
                    },
                requested,
                setValue
                    ? "country_leadership_value_invalid"
                    : "country_leadership_amount_invalid",
                error
            ))
        {
            return false;
        }
        int32_t requestedFixed = 0;
        if (!ScaleFixedPoint(
                requested,
                requestedFixed,
                setValue
                    ? "country_leadership_value"
                    : "country_leadership_amount",
                error
            )
            || (!setValue && requestedFixed == 0))
        {
            if (error.empty())
            {
                error = "country_leadership_amount_below_precision";
            }
            return false;
        }

        std::uintptr_t gameState = 0;
        std::uintptr_t country = 0;
        uint32_t countryIndex = 0;
        if (!ResolvePlayerCountryTarget(
                executableBase,
                effect,
                context,
                gameState,
                country,
                countryIndex,
                error
            ))
        {
            return false;
        }
        int32_t oldTotal = 0;
        if (!TryRead(country + CountryTotalLeadershipOffset, oldTotal))
        {
            error = "hoi3_country_leadership_unavailable";
            return false;
        }
        const int64_t desired64 = setValue
            ? requestedFixed
            : static_cast<int64_t>(oldTotal) + requestedFixed;
        if (desired64 < FixedPointScale
            || desired64 > std::numeric_limits<int32_t>::max())
        {
            error = "country_leadership_result_out_of_range";
            return false;
        }
        const int32_t desiredTotal = static_cast<int32_t>(desired64);

        int64_t sourceProvinceId = 0;
        if (const NativeEffectValue* value = FindFirst(
                effect,
                {"source_province_id", "province_id", "provinceid"}
            ))
        {
            if (!NativeEffectValueToInteger(*value, sourceProvinceId))
            {
                error = "country_leadership_source_province_invalid";
                return false;
            }
        }
        else
        {
            uint32_t capitalProvinceId = 0;
            if (!TryRead(
                    country + CountryCapitalProvinceOffset,
                    capitalProvinceId
                ))
            {
                error = "hoi3_country_capital_unavailable";
                return false;
            }
            sourceProvinceId = capitalProvinceId;
        }
        std::uintptr_t sourceProvince = 0;
        if (!ResolveProvince(
                gameState,
                sourceProvinceId,
                sourceProvince,
                error,
                context.safetyLease
            ))
        {
            return false;
        }
        int32_t oldSource = 0;
        int32_t sourceValue = 0;
        if (!TryRead(
                sourceProvince + ProvinceLeadershipOffset,
                oldSource
            )
            || !FindLeadershipSourceValue(
                gameState,
                country,
                sourceProvince,
                oldSource,
                oldTotal,
                desiredTotal,
                sourceValue,
                context.safetyLease
            ))
        {
            error = "country_leadership_target_not_representable";
            return false;
        }

        std::uint8_t* const base = executableBase;
        const auto state = std::make_shared<ApplyState>();
        prepared.apply = [
            base,
            country,
            sourceProvince,
            oldSource,
            sourceValue,
            oldTotal,
            desiredTotal,
            state
        ](std::string& applyError)
        {
            int32_t currentSource = 0;
            int32_t currentTotal = 0;
            if (!TryRead(
                    sourceProvince + ProvinceLeadershipOffset,
                    currentSource
                )
                || currentSource != oldSource
                || !TryRead(
                    country + CountryTotalLeadershipOffset,
                    currentTotal
                )
                || currentTotal != oldTotal)
            {
                applyError = "country_leadership_changed_before_apply";
                return false;
            }
            state->applied = true;
            if (!InvokeProvinceLeadershipSetter(
                    base,
                    sourceProvince,
                    sourceValue
                )
                || !TryWrite(
                    country + CountryTotalLeadershipOffset,
                    desiredTotal
                )
                || !TryRead(
                    sourceProvince + ProvinceLeadershipOffset,
                    currentSource
                )
                || currentSource != sourceValue
                || !TryRead(
                    country + CountryTotalLeadershipOffset,
                    currentTotal
                )
                || currentTotal != desiredTotal)
            {
                applyError = "country_leadership_native_apply_failed";
                return false;
            }
            applyError.clear();
            return true;
        };
        prepared.rollback = [
            base,
            country,
            sourceProvince,
            oldSource,
            oldTotal,
            state
        ]
        {
            if (!state->applied)
            {
                return;
            }
            InvokeProvinceLeadershipSetter(base, sourceProvince, oldSource);
            TryWrite(
                country + CountryTotalLeadershipOffset,
                oldTotal
            );
            state->applied = false;
        };
        error.clear();
        return true;
    }

    bool PrepareNamedCountryDefinition(
        const NativeEffect& effect,
        const NativeEffectExecutionContext& context,
        PreparedNativeEffect& prepared,
        std::string& error,
        NamedCountryDefinitionKind kind
    ) const
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        const bool government =
            kind == NamedCountryDefinitionKind::Government;
        const std::string prefix = government
            ? "country_government"
            : "country_ruling_ideology";
        std::string name;
        if (!ReadRequiredString(
                effect,
                government
                    ? std::initializer_list<std::string_view>{
                        "government", "government_name", "name", "value"
                    }
                    : std::initializer_list<std::string_view>{
                        "ideology", "ideology_name", "name", "value"
                    },
                name,
                prefix + "_name_invalid",
                error
            ))
        {
            return false;
        }
        void* const definitionPointer = FindNamedDefinition(
            executableBase,
            name,
            kind,
            error
        );
        if (!definitionPointer)
        {
            return false;
        }
        const std::uintptr_t definition =
            reinterpret_cast<std::uintptr_t>(definitionPointer);

        std::uintptr_t gameState = 0;
        std::uintptr_t country = 0;
        uint32_t countryIndex = 0;
        if (!ResolvePlayerCountryTarget(
                executableBase,
                effect,
                context,
                gameState,
                country,
                countryIndex,
                error
            ))
        {
            return false;
        }
        const std::uintptr_t field = country + (
            government
                ? CountryGovernmentOffset
                : CountryRulingIdeologyOffset
        );
        std::uintptr_t oldDefinition = 0;
        if (!TryRead(field, oldDefinition) || !oldDefinition)
        {
            error = "hoi3_" + prefix + "_unavailable";
            return false;
        }
        const std::uintptr_t setterRva = government
            ? Profile.countryGovernmentSetter
            : Profile.countryIdeologySetter;
        std::uint8_t* const base = executableBase;
        const auto state = std::make_shared<ApplyState>();
        prepared.apply = [
            base,
            country,
            field,
            oldDefinition,
            definition,
            setterRva,
            government,
            prefix,
            state
        ](std::string& applyError)
        {
            std::uintptr_t current = 0;
            if (!TryRead(field, current) || current != oldDefinition)
            {
                applyError = prefix + "_changed_before_apply";
                return false;
            }
            state->applied = true;
            if (!InvokeCountryDefinitionSetter(
                    base,
                    setterRva,
                    country,
                    definition
                )
                || (government
                    && !InvokeGovernmentRevalidate(base, country))
                || !TryRead(field, current)
                || current != definition)
            {
                applyError = prefix + "_native_apply_failed";
                return false;
            }
            applyError.clear();
            return true;
        };
        prepared.rollback = [
            base,
            country,
            setterRva,
            oldDefinition,
            government,
            state
        ]
        {
            if (!state->applied)
            {
                return;
            }
            InvokeCountryDefinitionSetter(
                base,
                setterRva,
                country,
                oldDefinition
            );
            if (government)
            {
                InvokeGovernmentRevalidate(base, country);
            }
            state->applied = false;
        };
        error.clear();
        return true;
    }

    bool PrepareIdeologyValue(
        const NativeEffect& effect,
        const NativeEffectExecutionContext& context,
        PreparedNativeEffect& prepared,
        std::string& error,
        IdeologyValueKind kind,
        bool setValue
    ) const
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        const std::string valueName =
            kind == IdeologyValueKind::Popularity
            ? "ideology_popularity"
            : "ideology_organization";
        std::string ideologyName;
        double requested = 0;
        if (!ReadRequiredString(
                effect,
                {"ideology", "ideology_name", "name"},
                ideologyName,
                "country_" + valueName + "_ideology_invalid",
                error
            )
            || !ReadRequiredNumber(
                effect,
                setValue
                    ? std::initializer_list<std::string_view>{
                        "value", "target", "amount"
                    }
                    : std::initializer_list<std::string_view>{
                        "amount", "delta"
                    },
                requested,
                setValue
                    ? "country_" + valueName + "_value_invalid"
                    : "country_" + valueName + "_amount_invalid",
                error
            ))
        {
            return false;
        }
        int32_t requestedFixed = 0;
        if (!ScaleFixedPoint(
                requested,
                requestedFixed,
                setValue
                    ? "country_" + valueName + "_value"
                    : "country_" + valueName + "_amount",
                error
            )
            || (!setValue && requestedFixed == 0))
        {
            if (error.empty())
            {
                error = "country_" + valueName
                    + "_amount_below_precision";
            }
            return false;
        }
        void* const ideologyPointer = FindNamedDefinition(
            executableBase,
            ideologyName,
            NamedCountryDefinitionKind::RulingIdeology,
            error
        );
        if (!ideologyPointer)
        {
            return false;
        }
        const std::uintptr_t ideology =
            reinterpret_cast<std::uintptr_t>(ideologyPointer);

        std::uintptr_t gameState = 0;
        std::uintptr_t country = 0;
        uint32_t countryIndex = 0;
        if (!ResolvePlayerCountryTarget(
                executableBase,
                effect,
                context,
                gameState,
                country,
                countryIndex,
                error
            ))
        {
            return false;
        }
        int32_t oldValue = 0;
        if (!ReadIdeologyValue(country, ideology, kind, oldValue))
        {
            error = "hoi3_country_" + valueName + "_unavailable";
            return false;
        }
        const int64_t expected64 = setValue
            ? requestedFixed
            : static_cast<int64_t>(oldValue) + requestedFixed;
        if (expected64 < 0 || expected64 > MaximumPercentage)
        {
            error = "country_" + valueName + "_result_out_of_range";
            return false;
        }
        const int32_t expected = static_cast<int32_t>(expected64);
        std::uint8_t* const base = executableBase;
        const auto state = std::make_shared<ApplyState>();
        prepared.apply = [
            base,
            country,
            countryIndex,
            ideology,
            kind,
            oldValue,
            expected,
            valueName,
            state
        ](std::string& applyError)
        {
            int32_t current = 0;
            if (!ReadIdeologyValue(country, ideology, kind, current)
                || current != oldValue)
            {
                applyError = "country_" + valueName
                    + "_changed_before_apply";
                return false;
            }
            state->applied = true;
            const int32_t delta = expected - oldValue;
            const int32_t organizationAmount =
                kind == IdeologyValueKind::Organization ? delta : 0;
            const int32_t popularityAmount =
                kind == IdeologyValueKind::Popularity ? delta : 0;
            if ((delta != 0 && !InvokeIdeologyValuesEffect(
                    base,
                    countryIndex,
                    ideology,
                    organizationAmount,
                    popularityAmount
                ))
                || !ReadIdeologyValue(country, ideology, kind, current)
                || current != expected)
            {
                applyError = "country_" + valueName
                    + "_native_apply_failed";
                return false;
            }
            applyError.clear();
            return true;
        };
        prepared.rollback = [
            base,
            country,
            countryIndex,
            ideology,
            kind,
            oldValue,
            state
        ]
        {
            if (!state->applied)
            {
                return;
            }
            int32_t current = 0;
            if (ReadIdeologyValue(country, ideology, kind, current))
            {
                const int32_t delta = oldValue - current;
                InvokeIdeologyValuesEffect(
                    base,
                    countryIndex,
                    ideology,
                    kind == IdeologyValueKind::Organization ? delta : 0,
                    kind == IdeologyValueKind::Popularity ? delta : 0
                );
            }
            state->applied = false;
        };
        error.clear();
        return true;
    }

    bool PrepareRelation(
        const NativeEffect& effect,
        const NativeEffectExecutionContext& context,
        PreparedNativeEffect& prepared,
        std::string& error,
        bool setValue
    ) const
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        double requested = 0;
        if (!ReadRequiredNumber(
                effect,
                setValue
                    ? std::initializer_list<std::string_view>{
                        "value", "target", "amount"
                    }
                    : std::initializer_list<std::string_view>{
                        "amount", "delta"
                    },
                requested,
                setValue
                    ? "diplomacy_relation_value_invalid"
                    : "diplomacy_relation_amount_invalid",
                error
            ))
        {
            return false;
        }
        int32_t requestedFixed = 0;
        if (!ScaleFixedPoint(
                requested,
                requestedFixed,
                setValue
                    ? "diplomacy_relation_value"
                    : "diplomacy_relation_amount",
                error
            )
            || (!setValue && requestedFixed == 0))
        {
            if (error.empty())
            {
                error = "diplomacy_relation_amount_below_precision";
            }
            return false;
        }

        std::uintptr_t gameState = 0;
        std::uintptr_t country = 0;
        uint32_t countryIndex = 0;
        if (!ResolvePlayerCountryTarget(
                executableBase,
                effect,
                context,
                gameState,
                country,
                countryIndex,
                error
            ))
        {
            return false;
        }
        std::uintptr_t targetCountry = 0;
        CountryTagValue targetTag{};
        if (!ResolveRequiredCountryByTag(
                executableBase,
                effect,
                {"target_tag", "other_tag", "country_target_tag"},
                targetCountry,
                targetTag,
                "diplomacy_target_tag_invalid",
                error,
                context.safetyLease
            ))
        {
            return false;
        }
        if (targetTag.index == countryIndex)
        {
            error = "diplomacy_target_is_source";
            return false;
        }
        std::uintptr_t forwardRelation = 0;
        std::uintptr_t reverseRelation = 0;
        int32_t oldForward = 0;
        int32_t oldReverse = 0;
        int32_t minimum = 0;
        int32_t maximum = 0;
        CountryTagValue sourceTag{};
        const std::uintptr_t baseAddress =
            reinterpret_cast<std::uintptr_t>(executableBase);
        if (!ReadCountryTagValue(country, sourceTag)
            || !ResolveRelationRecord(
                country,
                targetTag,
                forwardRelation,
                error,
                context.safetyLease
            )
            || !ResolveRelationRecord(
                targetCountry,
                sourceTag,
                reverseRelation,
                error,
                context.safetyLease
            )
            || !TryRead(forwardRelation + RelationValueOffset, oldForward)
            || !TryRead(reverseRelation + RelationValueOffset, oldReverse)
            || oldForward != oldReverse
            || !TryRead(
                baseAddress + Profile.minimumRelationValue,
                minimum
            )
            || !TryRead(
                baseAddress + Profile.maximumRelationValue,
                maximum
            )
            || minimum > maximum)
        {
            error = "hoi3_diplomacy_relation_unavailable";
            return false;
        }
        const int64_t expected64 = setValue
            ? requestedFixed
            : static_cast<int64_t>(oldForward) + requestedFixed;
        if (expected64 < minimum || expected64 > maximum)
        {
            error = "diplomacy_relation_result_out_of_range";
            return false;
        }
        const int32_t expected = static_cast<int32_t>(expected64);
        std::uint8_t* const base = executableBase;
        const auto state = std::make_shared<ApplyState>();
        prepared.apply = [
            base,
            country,
            targetTag,
            forwardRelation,
            reverseRelation,
            oldForward,
            expected,
            state
        ](std::string& applyError)
        {
            int32_t forward = 0;
            int32_t reverse = 0;
            if (!TryRead(forwardRelation + RelationValueOffset, forward)
                || !TryRead(reverseRelation + RelationValueOffset, reverse)
                || forward != oldForward
                || reverse != oldForward)
            {
                applyError = "diplomacy_relation_changed_before_apply";
                return false;
            }
            state->applied = true;
            const int32_t delta = expected - oldForward;
            if ((delta != 0
                    && !InvokeRelationAdd(base, country, targetTag, delta))
                || !TryRead(
                    forwardRelation + RelationValueOffset,
                    forward
                )
                || !TryRead(
                    reverseRelation + RelationValueOffset,
                    reverse
                )
                || forward != expected
                || reverse != expected)
            {
                applyError = "diplomacy_relation_native_apply_failed";
                return false;
            }
            applyError.clear();
            return true;
        };
        prepared.rollback = [
            base,
            country,
            targetTag,
            forwardRelation,
            oldForward,
            state
        ]
        {
            if (!state->applied)
            {
                return;
            }
            int32_t current = 0;
            if (TryRead(forwardRelation + RelationValueOffset, current)
                && current != oldForward)
            {
                InvokeRelationAdd(
                    base,
                    country,
                    targetTag,
                    oldForward - current
                );
            }
            state->applied = false;
        };
        error.clear();
        return true;
    }

    bool PrepareThreat(
        const NativeEffect& effect,
        const NativeEffectExecutionContext& context,
        PreparedNativeEffect& prepared,
        std::string& error,
        bool setValue
    ) const
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        double requested = 0;
        if (!ReadRequiredNumber(
                effect,
                setValue
                    ? std::initializer_list<std::string_view>{
                        "value", "target", "amount"
                    }
                    : std::initializer_list<std::string_view>{
                        "amount", "delta"
                    },
                requested,
                setValue
                    ? "diplomacy_threat_value_invalid"
                    : "diplomacy_threat_amount_invalid",
                error
            ))
        {
            return false;
        }
        int32_t requestedFixed = 0;
        if (!ScaleFixedPoint(
                requested,
                requestedFixed,
                setValue
                    ? "diplomacy_threat_value"
                    : "diplomacy_threat_amount",
                error
            )
            || (!setValue && requestedFixed == 0))
        {
            if (error.empty())
            {
                error = "diplomacy_threat_amount_below_precision";
            }
            return false;
        }

        std::uintptr_t gameState = 0;
        std::uintptr_t country = 0;
        uint32_t countryIndex = 0;
        if (!ResolvePlayerCountryTarget(
                executableBase,
                effect,
                context,
                gameState,
                country,
                countryIndex,
                error
            ))
        {
            return false;
        }
        std::uintptr_t targetCountry = 0;
        CountryTagValue targetTag{};
        if (!ResolveRequiredCountryByTag(
                executableBase,
                effect,
                {"target_tag", "other_tag", "country_target_tag"},
                targetCountry,
                targetTag,
                "diplomacy_target_tag_invalid",
                error,
                context.safetyLease
            ))
        {
            return false;
        }
        if (targetTag.index == countryIndex)
        {
            error = "diplomacy_target_is_source";
            return false;
        }
        std::uintptr_t relation = 0;
        int32_t oldValue = 0;
        if (!ResolveRelationRecord(
                country,
                targetTag,
                relation,
                error,
                context.safetyLease
            )
            || !TryRead(relation + RelationThreatOffset, oldValue))
        {
            error = "hoi3_diplomacy_threat_unavailable";
            return false;
        }
        const int64_t expected64 = setValue
            ? requestedFixed
            : static_cast<int64_t>(oldValue) + requestedFixed;
        if (expected64 < 0
            || expected64 > std::numeric_limits<int32_t>::max())
        {
            error = "diplomacy_threat_result_out_of_range";
            return false;
        }
        const int32_t expected = static_cast<int32_t>(expected64);
        std::uint8_t* const base = executableBase;
        const auto state = std::make_shared<ApplyState>();
        prepared.apply = [
            base,
            countryIndex,
            targetTag,
            relation,
            oldValue,
            expected,
            state
        ](std::string& applyError)
        {
            int32_t current = 0;
            if (!TryRead(relation + RelationThreatOffset, current)
                || current != oldValue)
            {
                applyError = "diplomacy_threat_changed_before_apply";
                return false;
            }
            state->applied = true;
            const int32_t delta = expected - oldValue;
            if ((delta != 0 && !InvokeThreatEffect(
                    base,
                    countryIndex,
                    targetTag.index,
                    delta
                ))
                || !TryRead(relation + RelationThreatOffset, current)
                || current != expected)
            {
                applyError = "diplomacy_threat_native_apply_failed";
                return false;
            }
            applyError.clear();
            return true;
        };
        prepared.rollback = [
            base,
            countryIndex,
            targetTag,
            relation,
            oldValue,
            state
        ]
        {
            if (!state->applied)
            {
                return;
            }
            int32_t current = 0;
            if (TryRead(relation + RelationThreatOffset, current)
                && current != oldValue)
            {
                InvokeThreatEffect(
                    base,
                    countryIndex,
                    targetTag.index,
                    oldValue - current
                );
            }
            state->applied = false;
        };
        error.clear();
        return true;
    }

    bool PrepareSpyPresence(
        const NativeEffect& effect,
        const NativeEffectExecutionContext& context,
        PreparedNativeEffect& prepared,
        std::string& error
    ) const
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        double requested = 0;
        if (!ReadRequiredNumber(
                effect,
                {"level", "value", "target", "amount"},
                requested,
                "espionage_presence_level_invalid",
                error
            ))
        {
            return false;
        }
        int32_t expected = 0;
        if (!ScaleFixedPoint(
                requested,
                expected,
                "espionage_presence_level",
                error
            ))
        {
            return false;
        }
        std::uintptr_t gameState = 0;
        std::uintptr_t country = 0;
        uint32_t countryIndex = 0;
        if (!ResolvePlayerCountryTarget(
                executableBase,
                effect,
                context,
                gameState,
                country,
                countryIndex,
                error
            ))
        {
            return false;
        }
        std::uintptr_t targetCountry = 0;
        CountryTagValue targetTag{};
        if (!ResolveRequiredCountryByTag(
                executableBase,
                effect,
                {"target_tag", "other_tag", "country_target_tag"},
                targetCountry,
                targetTag,
                "espionage_target_tag_invalid",
                error,
                context.safetyLease
            ))
        {
            return false;
        }
        int32_t maximum = 0;
        const std::uintptr_t baseAddress =
            reinterpret_cast<std::uintptr_t>(executableBase);
        std::uintptr_t presence = 0;
        int32_t oldValue = 0;
        if (!ResolveSpyPresence(country, targetTag.index, presence)
            || !TryRead(presence + SpyPresenceLevelOffset, oldValue)
            || !TryRead(
                baseAddress + Profile.maximumSpyPresenceLevel,
                maximum
            )
            || expected < 0
            || expected > maximum)
        {
            error = "espionage_presence_level_out_of_range";
            return false;
        }
        const std::uintptr_t field = presence + SpyPresenceLevelOffset;
        const auto state = std::make_shared<ApplyState>();
        prepared.apply = [field, oldValue, expected, state](
            std::string& applyError
        )
        {
            int32_t current = 0;
            if (!TryRead(field, current) || current != oldValue)
            {
                applyError = "espionage_presence_changed_before_apply";
                return false;
            }
            state->applied = true;
            if (!TryWrite(field, expected)
                || !TryRead(field, current)
                || current != expected)
            {
                applyError = "espionage_presence_native_apply_failed";
                return false;
            }
            applyError.clear();
            return true;
        };
        prepared.rollback = [field, oldValue, state]
        {
            if (!state->applied)
            {
                return;
            }
            TryWrite(field, oldValue);
            state->applied = false;
        };
        error.clear();
        return true;
    }

    bool PrepareProvinceIntel(
        const NativeEffect& effect,
        const NativeEffectExecutionContext& context,
        PreparedNativeEffect& prepared,
        std::string& error
    ) const
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        int64_t provinceId = 0;
        int64_t requestedLevel = 0;
        if (!ReadRequiredInteger(
                effect,
                {"province_id", "provinceid", "id"},
                provinceId,
                "intelligence_province_id_invalid",
                error
            )
            || !ReadRequiredInteger(
                effect,
                {"level", "value", "target", "amount"},
                requestedLevel,
                "intelligence_province_level_invalid",
                error
            )
            || requestedLevel < 0
            || requestedLevel > 10)
        {
            if (error.empty())
            {
                error = "intelligence_province_level_out_of_range";
            }
            return false;
        }

        std::uintptr_t gameState = 0;
        std::uintptr_t country = 0;
        uint32_t countryIndex = 0;
        if (!ResolvePlayerCountryTarget(
                executableBase,
                effect,
                context,
                gameState,
                country,
                countryIndex,
                error
            ))
        {
            return false;
        }
        std::uintptr_t province = 0;
        if (!ResolveProvince(
                gameState,
                provinceId,
                province,
                error,
                context.safetyLease
            ))
        {
            return false;
        }
        std::uintptr_t field = 0;
        std::uint8_t oldValue = 0;
        if (!ResolveProvinceIntelLevel(province, countryIndex, field)
            || !TryRead(field, oldValue))
        {
            error = "hoi3_province_intelligence_unavailable";
            return false;
        }
        const std::uint8_t expected = static_cast<std::uint8_t>(
            requestedLevel
        );
        const auto state = std::make_shared<ApplyState>();
        prepared.apply = [
            field,
            oldValue,
            expected,
            state
        ](std::string& applyError)
        {
            std::uint8_t current = 0;
            if (!TryRead(field, current) || current != oldValue)
            {
                applyError = "intelligence_province_changed_before_apply";
                return false;
            }
            state->applied = true;
            if (!TryWrite(field, expected)
                || !TryRead(field, current)
                || current != expected)
            {
                applyError = "intelligence_province_native_apply_failed";
                return false;
            }
            applyError.clear();
            return true;
        };
        prepared.rollback = [
            field,
            oldValue,
            state
        ]
        {
            if (!state->applied)
            {
                return;
            }
            TryWrite(field, oldValue);
            state->applied = false;
        };
        error.clear();
        return true;
    }

    bool PrepareCountryGoods(
        const NativeEffect& effect,
        const NativeEffectExecutionContext& context,
        PreparedNativeEffect& prepared,
        std::string& error,
        bool setValue
    ) const
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        std::string goodsName;
        GoodsSpec spec;
        if (!ReadRequiredString(
                effect,
                {"goods", "good", "resource", "resource_name", "name"},
                goodsName,
                "country_goods_name_invalid",
                error
            ))
        {
            return false;
        }
        if (!ResolveGoodsSpec(goodsName, spec))
        {
            error = "country_goods_name_unsupported: " + goodsName;
            return false;
        }
        double requested = 0;
        const bool hasValue = setValue
            ? ReadRequiredNumber(
                effect,
                {"value", "target", "amount"},
                requested,
                "country_goods_value_invalid",
                error
            )
            : ReadRequiredNumber(
                effect,
                {"amount", "delta"},
                requested,
                "country_goods_amount_invalid",
                error
            );
        if (!hasValue)
        {
            return false;
        }
        int32_t requestedFixed = 0;
        if (!ScaleFixedPoint(
                requested,
                requestedFixed,
                setValue ? "country_goods_value" : "country_goods_amount",
                error
            ))
        {
            return false;
        }
        if (!setValue && requestedFixed == 0)
        {
            error = "country_goods_amount_below_precision";
            return false;
        }

        std::uintptr_t gameState = 0;
        std::uintptr_t country = 0;
        uint32_t countryIndex = 0;
        if (!ResolvePlayerCountryTarget(
                executableBase,
                effect,
                context,
                gameState,
                country,
                countryIndex,
                error
            ))
        {
            return false;
        }
        std::uintptr_t pool = 0;
        if (!ResolveCountryGoodsPool(
                executableBase,
                country,
                pool,
                error
            ))
        {
            return false;
        }
        const std::uintptr_t field = pool + spec.fieldOffset;
        int32_t oldValue = 0;
        if (!TryRead(field, oldValue))
        {
            error = "hoi3_country_goods_value_unavailable";
            return false;
        }
        const int64_t expected64 = setValue
            ? requestedFixed
            : static_cast<int64_t>(oldValue) + requestedFixed;
        if (expected64 < 0
            || expected64 > std::numeric_limits<int32_t>::max())
        {
            error = "country_goods_result_out_of_range";
            return false;
        }
        const int32_t expected = static_cast<int32_t>(expected64);
        const int64_t delta64 = static_cast<int64_t>(expected) - oldValue;
        if (delta64 < std::numeric_limits<int32_t>::min()
            || delta64 > std::numeric_limits<int32_t>::max())
        {
            error = "country_goods_delta_out_of_range";
            return false;
        }
        const int32_t delta = static_cast<int32_t>(delta64);
        const std::uintptr_t executeRva = spec.executeRva;
        const std::string goods = spec.name;
        std::uint8_t* const base = executableBase;
        const auto state = std::make_shared<ApplyState>();
        prepared.apply = [
            base,
            field,
            countryIndex,
            executeRva,
            delta,
            oldValue,
            expected,
            goods,
            state
        ](std::string& applyError)
        {
            int32_t current = 0;
            if (!TryRead(field, current) || current != oldValue)
            {
                applyError = "country_goods_" + goods
                    + "_changed_before_apply";
                return false;
            }
            if ((delta != 0 && !InvokeCountryValueEffect(
                    base,
                    executeRva,
                    countryIndex,
                    delta
                ))
                || !TryRead(field, current)
                || current != expected)
            {
                applyError = "country_goods_" + goods
                    + "_native_apply_failed";
                return false;
            }
            state->applied = true;
            applyError.clear();
            return true;
        };
        prepared.rollback = [
            base,
            field,
            countryIndex,
            executeRva,
            oldValue,
            state
        ]
        {
            if (!state->applied)
            {
                return;
            }
            int32_t current = 0;
            if (!TryRead(field, current))
            {
                return;
            }
            const int64_t rollback64 = static_cast<int64_t>(oldValue)
                - current;
            if (rollback64 >= std::numeric_limits<int32_t>::min()
                && rollback64 <= std::numeric_limits<int32_t>::max()
                && rollback64 != 0)
            {
                InvokeCountryValueEffect(
                    base,
                    executeRva,
                    countryIndex,
                    static_cast<int32_t>(rollback64)
                );
            }
            state->applied = false;
        };
        error.clear();
        return true;
    }

    bool PrepareCountryNationalUnity(
        const NativeEffect& effect,
        const NativeEffectExecutionContext& context,
        PreparedNativeEffect& prepared,
        std::string& error,
        bool setValue
    ) const
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        double requested = 0;
        const bool hasValue = setValue
            ? ReadRequiredNumber(
                effect,
                {"value", "target", "amount"},
                requested,
                "country_national_unity_value_invalid",
                error
            )
            : ReadRequiredNumber(
                effect,
                {"amount", "delta"},
                requested,
                "country_national_unity_amount_invalid",
                error
            );
        if (!hasValue)
        {
            return false;
        }
        int32_t requestedFixed = 0;
        if (!ScaleFixedPoint(
                requested,
                requestedFixed,
                setValue
                    ? "country_national_unity_value"
                    : "country_national_unity_amount",
                error
            ))
        {
            return false;
        }

        std::uintptr_t gameState = 0;
        std::uintptr_t country = 0;
        uint32_t countryIndex = 0;
        if (!ResolvePlayerCountryTarget(
                executableBase,
                effect,
                context,
                gameState,
                country,
                countryIndex,
                error
            ))
        {
            return false;
        }
        int32_t oldValue = 0;
        std::uintptr_t modifierSource = 0;
        int32_t modifier = 0;
        if (!TryRead(country + CountryNationalUnityOffset, oldValue)
            || !TryRead(
                country + CountryNationalUnitySourceOffset,
                modifierSource
            )
            || !modifierSource
            || !TryRead(
                modifierSource + NationalUnityModifierOffset,
                modifier
            ))
        {
            error = "hoi3_country_national_unity_unavailable";
            return false;
        }
        const int64_t factor64 = static_cast<int64_t>(modifier)
            + FixedPointScale;
        if (factor64 <= 0
            || factor64 > std::numeric_limits<int32_t>::max())
        {
            error = "country_national_unity_modifier_invalid";
            return false;
        }
        const int32_t factor = static_cast<int32_t>(factor64);
        int32_t nativeAmount = requestedFixed;
        int32_t expectedChange = 0;
        int32_t expected = 0;
        if (setValue)
        {
            if (requestedFixed < FixedPointScale
                || requestedFixed > MaximumPercentage)
            {
                error = "country_national_unity_result_out_of_range";
                return false;
            }
            expected = requestedFixed;
            const int64_t desiredChange64 = static_cast<int64_t>(expected)
                - oldValue;
            if (desiredChange64 < std::numeric_limits<int32_t>::min()
                || desiredChange64 > std::numeric_limits<int32_t>::max()
                || !FindNationalUnityNativeAmount(
                    static_cast<int32_t>(desiredChange64),
                    factor,
                    nativeAmount
                ))
            {
                error = "country_national_unity_value_not_representable";
                return false;
            }
        }
        else
        {
            if (nativeAmount == 0
                || !ComputeNationalUnityChange(
                    nativeAmount,
                    factor,
                    expectedChange
                )
                || expectedChange == 0)
            {
                error = "country_national_unity_amount_below_precision";
                return false;
            }
            const int64_t expected64 = static_cast<int64_t>(oldValue)
                + expectedChange;
            if (expected64 < FixedPointScale
                || expected64 > MaximumPercentage)
            {
                error = "country_national_unity_result_out_of_range";
                return false;
            }
            expected = static_cast<int32_t>(expected64);
        }
        if (nativeAmount == std::numeric_limits<int32_t>::min())
        {
            error = "country_national_unity_native_amount_out_of_range";
            return false;
        }

        const std::uintptr_t factorAddress = modifierSource
            + NationalUnityModifierOffset;
        std::uint8_t* const base = executableBase;
        const auto state = std::make_shared<ApplyState>();
        prepared.apply = [
            base,
            country,
            countryIndex,
            factorAddress,
            factor,
            nativeAmount,
            oldValue,
            expected,
            state
        ](std::string& applyError)
        {
            int32_t current = 0;
            int32_t currentModifier = 0;
            if (!TryRead(country + CountryNationalUnityOffset, current)
                || current != oldValue
                || !TryRead(factorAddress, currentModifier)
                || static_cast<int64_t>(currentModifier)
                    + FixedPointScale != factor)
            {
                applyError = "country_national_unity_changed_before_apply";
                return false;
            }
            if ((nativeAmount != 0 && !InvokeCountryValueEffect(
                    base,
                    Profile.nationalUnityEffectExecute,
                    countryIndex,
                    nativeAmount
                ))
                || !TryRead(
                    country + CountryNationalUnityOffset,
                    current
                )
                || current != expected)
            {
                applyError = "country_national_unity_native_apply_failed";
                return false;
            }
            state->applied = true;
            applyError.clear();
            return true;
        };
        prepared.rollback = [
            base,
            countryIndex,
            nativeAmount,
            state
        ]
        {
            if (state->applied && nativeAmount != 0)
            {
                InvokeCountryValueEffect(
                    base,
                    Profile.nationalUnityEffectExecute,
                    countryIndex,
                    -nativeAmount
                );
            }
            state->applied = false;
        };
        error.clear();
        return true;
    }

    bool PrepareCountryModifier(
        const NativeEffect& effect,
        const NativeEffectExecutionContext& context,
        PreparedNativeEffect& prepared,
        std::string& error,
        bool addModifier
    ) const
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        std::string modifierName;
        int64_t durationDays = -1;
        if (!ReadRequiredString(
                effect,
                {"modifier", "modifier_name", "name"},
                modifierName,
                "country_modifier_name_invalid",
                error
            ))
        {
            return false;
        }
        if (addModifier
            && !ReadRequiredInteger(
                effect,
                {"duration_days", "durationdays", "duration"},
                durationDays,
                "country_modifier_duration_invalid",
                error
            ))
        {
            return false;
        }
        if (addModifier
            && ((durationDays != -1 && durationDays <= 0)
                || durationDays > MaximumModifierDurationDays))
        {
            error = "country_modifier_duration_out_of_range";
            return false;
        }

        std::uintptr_t gameState = 0;
        std::uintptr_t country = 0;
        uint32_t countryIndex = 0;
        if (!ResolvePlayerCountryTarget(
                executableBase,
                effect,
                context,
                gameState,
                country,
                countryIndex,
                error
            ))
        {
            return false;
        }
        void* definitionPointer = FindModifierDefinition(
            executableBase,
            modifierName,
            error
        );
        if (!definitionPointer)
        {
            return false;
        }
        const std::uintptr_t definition = reinterpret_cast<std::uintptr_t>(
            definitionPointer
        );
        ModifierInstance existing;
        if (!FindCountryModifierInstance(country, definition, existing))
        {
            error = "hoi3_country_modifier_list_unavailable";
            return false;
        }
        if (addModifier == existing.found)
        {
            error = addModifier
                ? "country_modifier_already_present"
                : "country_modifier_missing";
            return false;
        }

        const int32_t nativeDuration = static_cast<int32_t>(durationDays);
        const int32_t originalExpiry = existing.expiry;
        std::uint8_t* const base = executableBase;
        const auto state = std::make_shared<ApplyState>();
        prepared.apply = [
            base,
            country,
            countryIndex,
            definition,
            nativeDuration,
            addModifier,
            state
        ](std::string& applyError)
        {
            ModifierInstance current;
            if (!FindCountryModifierInstance(country, definition, current)
                || current.found != !addModifier)
            {
                applyError = "country_modifier_changed_before_apply";
                return false;
            }
            const bool invoked = addModifier
                ? InvokeAddCountryModifierEffect(
                    base,
                    countryIndex,
                    definition,
                    nativeDuration
                )
                : InvokeRemoveCountryModifierEffect(
                    base,
                    countryIndex,
                    definition
                );
            if (!invoked
                || !FindCountryModifierInstance(
                    country,
                    definition,
                    current
                )
                || current.found != addModifier)
            {
                applyError = addModifier
                    ? "country_modifier_native_add_failed"
                    : "country_modifier_native_remove_failed";
                return false;
            }
            state->applied = true;
            applyError.clear();
            return true;
        };
        prepared.rollback = [
            base,
            country,
            countryIndex,
            definition,
            originalExpiry,
            addModifier,
            state
        ]
        {
            if (!state->applied)
            {
                return;
            }
            if (addModifier)
            {
                InvokeRemoveCountryModifierEffect(
                    base,
                    countryIndex,
                    definition
                );
            }
            else
            {
                RestoreCountryModifier(
                    base,
                    country,
                    countryIndex,
                    definition,
                    originalExpiry
                );
            }
            state->applied = false;
        };
        error.clear();
        return true;
    }

    bool PrepareProvinceAddModifier(
        const NativeEffect& effect,
        const NativeEffectExecutionContext& context,
        PreparedNativeEffect& prepared,
        std::string& error
    ) const
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        int64_t provinceId = 0;
        int64_t durationDays = 0;
        std::string modifierName;
        if (!ReadRequiredInteger(
                effect,
                {"province_id", "provinceid", "id"},
                provinceId,
                "province_id_invalid",
                error
            )
            || !ReadRequiredString(
                effect,
                {"modifier", "modifier_name", "name"},
                modifierName,
                "province_modifier_name_invalid",
                error
            )
            || !ReadRequiredInteger(
                effect,
                {"duration_days", "durationdays", "duration"},
                durationDays,
                "province_modifier_duration_invalid",
                error
            ))
        {
            return false;
        }
        if ((durationDays != -1 && durationDays <= 0)
            || durationDays > MaximumModifierDurationDays)
        {
            error = "province_modifier_duration_out_of_range";
            return false;
        }

        std::uintptr_t gameState = 0;
        std::uintptr_t province = 0;
        if (!ResolveGameState(executableBase, gameState))
        {
            error = "hoi3_game_state_unavailable";
            return false;
        }
        if (!ResolveProvince(
                gameState,
                provinceId,
                province,
                error,
                context.safetyLease
            ))
        {
            return false;
        }
        void* definitionPointer = FindModifierDefinition(
            executableBase,
            modifierName,
            error
        );
        if (!definitionPointer)
        {
            return false;
        }
        const std::uintptr_t definition = reinterpret_cast<std::uintptr_t>(
            definitionPointer
        );
        ModifierInstance existing;
        if (!FindProvinceModifierInstance(province, definition, existing))
        {
            error = "hoi3_province_modifier_list_unavailable";
            return false;
        }
        if (existing.found)
        {
            error = "province_modifier_already_present";
            return false;
        }

        const auto state = std::make_shared<ApplyState>();
        std::uint8_t* const base = executableBase;
        const uint32_t nativeProvinceId = static_cast<uint32_t>(provinceId);
        const int32_t nativeDuration = static_cast<int32_t>(durationDays);
        prepared.apply = [
            base,
            province,
            nativeProvinceId,
            definition,
            nativeDuration,
            state
        ](std::string& applyError)
        {
            ModifierInstance current;
            if (!FindProvinceModifierInstance(province, definition, current))
            {
                applyError = "hoi3_province_modifier_list_unavailable";
                return false;
            }
            if (current.found)
            {
                applyError = "province_modifier_changed_before_apply";
                return false;
            }
            if (!InvokeAddProvinceModifierEffect(
                    base,
                    nativeProvinceId,
                    definition,
                    nativeDuration
                )
                || !FindProvinceModifierInstance(province, definition, current)
                || !current.found)
            {
                applyError = "province_modifier_native_add_failed";
                return false;
            }
            state->applied = true;
            applyError.clear();
            return true;
        };
        prepared.rollback = [
            base,
            nativeProvinceId,
            definition,
            state
        ]
        {
            if (!state->applied)
            {
                return;
            }
            InvokeRemoveProvinceModifierEffect(
                base,
                nativeProvinceId,
                definition
            );
            state->applied = false;
        };
        error.clear();
        return true;
    }

    bool PrepareProvinceRemoveModifier(
        const NativeEffect& effect,
        const NativeEffectExecutionContext& context,
        PreparedNativeEffect& prepared,
        std::string& error
    ) const
    {
        if (!supported || !executableBase)
        {
            error = "hoi3_native_effects_unsupported_executable";
            return false;
        }
        int64_t provinceId = 0;
        std::string modifierName;
        if (!ReadRequiredInteger(
                effect,
                {"province_id", "provinceid", "id"},
                provinceId,
                "province_id_invalid",
                error
            )
            || !ReadRequiredString(
                effect,
                {"modifier", "modifier_name", "name"},
                modifierName,
                "province_modifier_name_invalid",
                error
            ))
        {
            return false;
        }

        std::uintptr_t gameState = 0;
        std::uintptr_t province = 0;
        if (!ResolveGameState(executableBase, gameState))
        {
            error = "hoi3_game_state_unavailable";
            return false;
        }
        if (!ResolveProvince(
                gameState,
                provinceId,
                province,
                error,
                context.safetyLease
            ))
        {
            return false;
        }
        void* definitionPointer = FindModifierDefinition(
            executableBase,
            modifierName,
            error
        );
        if (!definitionPointer)
        {
            return false;
        }
        const std::uintptr_t definition = reinterpret_cast<std::uintptr_t>(
            definitionPointer
        );
        ModifierInstance existing;
        if (!FindProvinceModifierInstance(province, definition, existing))
        {
            error = "hoi3_province_modifier_list_unavailable";
            return false;
        }
        if (!existing.found)
        {
            error = "province_modifier_missing";
            return false;
        }

        const auto state = std::make_shared<ApplyState>();
        std::uint8_t* const base = executableBase;
        const uint32_t nativeProvinceId = static_cast<uint32_t>(provinceId);
        const int32_t originalExpiry = existing.expiry;
        prepared.apply = [
            base,
            province,
            nativeProvinceId,
            definition,
            state
        ](std::string& applyError)
        {
            ModifierInstance current;
            if (!FindProvinceModifierInstance(province, definition, current)
                || !current.found)
            {
                applyError = "province_modifier_changed_before_apply";
                return false;
            }
            if (!InvokeRemoveProvinceModifierEffect(
                    base,
                    nativeProvinceId,
                    definition
                )
                || !FindProvinceModifierInstance(province, definition, current)
                || current.found)
            {
                applyError = "province_modifier_native_remove_failed";
                return false;
            }
            state->applied = true;
            applyError.clear();
            return true;
        };
        prepared.rollback = [
            base,
            province,
            nativeProvinceId,
            definition,
            originalExpiry,
            state
        ]
        {
            if (!state->applied)
            {
                return;
            }
            RestoreProvinceModifier(
                base,
                province,
                nativeProvinceId,
                definition,
                originalExpiry
            );
            state->applied = false;
        };
        error.clear();
        return true;
    }
};

Hoi3GameplayEffects::Hoi3GameplayEffects()
    : impl_(std::make_unique<Impl>())
{
#if defined(_MSC_VER) && defined(_M_IX86)
    std::string registryError;
    auto& registry = engine::GetEngineRegistry();
    impl_->supported = registry.InitializeCurrentProcess(registryError);
    impl_->executableBase = reinterpret_cast<std::uint8_t*>(
        registry.ModuleBase()
    );
#else
    impl_->supported = false;
#endif
}

Hoi3GameplayEffects::~Hoi3GameplayEffects()
{
    UnregisterHandlers();
}

bool Hoi3GameplayEffects::RegisterHandlers(
    NativeEffectService& service,
    std::string& error
)
{
    if (impl_->service == &service)
    {
        error.clear();
        return true;
    }
    if (impl_->service)
    {
        error = "hoi3_gameplay_effects_already_registered";
        return false;
    }

    impl_->registeredOperations.clear();
    const auto rollbackRegistration = [&]
    {
        for (auto operation = impl_->registeredOperations.rbegin();
             operation != impl_->registeredOperations.rend();
             ++operation)
        {
            service.UnregisterHandler(*operation);
        }
        impl_->registeredOperations.clear();
    };
    const auto registerHandler = [
        &service,
        &error,
        &rollbackRegistration,
        this
    ](
        const char* operation,
        NativeEffectPrepareHandler handler
    )
    {
        if (!service.RegisterHandler(operation, std::move(handler), error))
        {
            rollbackRegistration();
            return false;
        }
        impl_->registeredOperations.emplace_back(operation);
        return true;
    };
    const auto scalarHandler = [this](
        CountryScalarKind kind,
        bool setValue
    )
    {
        return [this, kind, setValue](
            const NativeEffect& effect,
            const NativeEffectExecutionContext& context,
            PreparedNativeEffect& prepared,
            std::string& handlerError
        )
        {
            return impl_->PrepareCountryScalar(
                effect,
                context,
                prepared,
                handlerError,
                kind,
                setValue
            );
        };
    };
    const auto goodsHandler = [this](bool setValue)
    {
        return [this, setValue](
            const NativeEffect& effect,
            const NativeEffectExecutionContext& context,
            PreparedNativeEffect& prepared,
            std::string& handlerError
        )
        {
            return impl_->PrepareCountryGoods(
                effect,
                context,
                prepared,
                handlerError,
                setValue
            );
        };
    };
    const auto directScalarHandler = [this](
        DirectCountryScalarKind kind,
        bool setValue
    )
    {
        return [this, kind, setValue](
            const NativeEffect& effect,
            const NativeEffectExecutionContext& context,
            PreparedNativeEffect& prepared,
            std::string& handlerError
        )
        {
            return impl_->PrepareDirectCountryScalar(
                effect,
                context,
                prepared,
                handlerError,
                kind,
                setValue
            );
        };
    };
    const auto diplomaticInfluenceHandler = [this](bool setValue)
    {
        return [this, setValue](
            const NativeEffect& effect,
            const NativeEffectExecutionContext& context,
            PreparedNativeEffect& prepared,
            std::string& handlerError
        )
        {
            return impl_->PrepareDiplomaticInfluence(
                effect,
                context,
                prepared,
                handlerError,
                setValue
            );
        };
    };
    const auto leadershipHandler = [this](bool setValue)
    {
        return [this, setValue](
            const NativeEffect& effect,
            const NativeEffectExecutionContext& context,
            PreparedNativeEffect& prepared,
            std::string& handlerError
        )
        {
            return impl_->PrepareLeadership(
                effect,
                context,
                prepared,
                handlerError,
                setValue
            );
        };
    };
    const auto definitionHandler = [this](
        NamedCountryDefinitionKind kind
    )
    {
        return [this, kind](
            const NativeEffect& effect,
            const NativeEffectExecutionContext& context,
            PreparedNativeEffect& prepared,
            std::string& handlerError
        )
        {
            return impl_->PrepareNamedCountryDefinition(
                effect,
                context,
                prepared,
                handlerError,
                kind
            );
        };
    };
    const auto ideologyValueHandler = [this](
        IdeologyValueKind kind,
        bool setValue
    )
    {
        return [this, kind, setValue](
            const NativeEffect& effect,
            const NativeEffectExecutionContext& context,
            PreparedNativeEffect& prepared,
            std::string& handlerError
        )
        {
            return impl_->PrepareIdeologyValue(
                effect,
                context,
                prepared,
                handlerError,
                kind,
                setValue
            );
        };
    };
    const auto relationHandler = [this](bool setValue)
    {
        return [this, setValue](
            const NativeEffect& effect,
            const NativeEffectExecutionContext& context,
            PreparedNativeEffect& prepared,
            std::string& handlerError
        )
        {
            return impl_->PrepareRelation(
                effect,
                context,
                prepared,
                handlerError,
                setValue
            );
        };
    };
    const auto threatHandler = [this](bool setValue)
    {
        return [this, setValue](
            const NativeEffect& effect,
            const NativeEffectExecutionContext& context,
            PreparedNativeEffect& prepared,
            std::string& handlerError
        )
        {
            return impl_->PrepareThreat(
                effect,
                context,
                prepared,
                handlerError,
                setValue
            );
        };
    };
    const auto nationalUnityHandler = [this](bool setValue)
    {
        return [this, setValue](
            const NativeEffect& effect,
            const NativeEffectExecutionContext& context,
            PreparedNativeEffect& prepared,
            std::string& handlerError
        )
        {
            return impl_->PrepareCountryNationalUnity(
                effect,
                context,
                prepared,
                handlerError,
                setValue
            );
        };
    };
    const auto countryModifierHandler = [this](bool addModifier)
    {
        return [this, addModifier](
            const NativeEffect& effect,
            const NativeEffectExecutionContext& context,
            PreparedNativeEffect& prepared,
            std::string& handlerError
        )
        {
            return impl_->PrepareCountryModifier(
                effect,
                context,
                prepared,
                handlerError,
                addModifier
            );
        };
    };

    if (!registerHandler(
            "global.set_flag",
            [this](
                const NativeEffect& effect,
                const NativeEffectExecutionContext&,
                PreparedNativeEffect& prepared,
                std::string& handlerError
            )
            {
                return impl_->PrepareGlobalFlag(
                    effect,
                    prepared,
                    handlerError,
                    true
                );
            }
        )
        || !registerHandler(
            "global.clear_flag",
            [this](
                const NativeEffect& effect,
                const NativeEffectExecutionContext&,
                PreparedNativeEffect& prepared,
                std::string& handlerError
            )
            {
                return impl_->PrepareGlobalFlag(
                    effect,
                    prepared,
                    handlerError,
                    false
                );
            }
        )
        || !registerHandler(
            "event.fire",
            [this](
                const NativeEffect& effect,
                const NativeEffectExecutionContext& context,
                PreparedNativeEffect& prepared,
                std::string& handlerError
            )
            {
                return impl_->PrepareEventFire(
                    effect,
                    context,
                    prepared,
                    handlerError
                );
            }
        )
        || !registerHandler(
            "event.execute",
            [this](
                const NativeEffect& effect,
                const NativeEffectExecutionContext& context,
                PreparedNativeEffect& prepared,
                std::string& handlerError
            )
            {
                return impl_->PrepareEventFire(
                    effect,
                    context,
                    prepared,
                    handlerError
                );
            }
        )
        || !registerHandler(
            "event.enqueue",
            [this](
                const NativeEffect& effect,
                const NativeEffectExecutionContext& context,
                PreparedNativeEffect& prepared,
                std::string& handlerError
            )
            {
                return impl_->PrepareDispatchEnqueue(
                    effect,
                    context,
                    prepared,
                    handlerError,
                    Impl::DispatchKind::Event
                );
            }
        )
        || !registerHandler(
            "event.cancel",
            [this](
                const NativeEffect& effect,
                const NativeEffectExecutionContext&,
                PreparedNativeEffect& prepared,
                std::string& handlerError
            )
            {
                return impl_->PrepareDispatchCancel(
                    effect,
                    prepared,
                    handlerError,
                    Impl::DispatchKind::Event
                );
            }
        )
        || !registerHandler(
            "decision.execute",
            [this](
                const NativeEffect& effect,
                const NativeEffectExecutionContext& context,
                PreparedNativeEffect& prepared,
                std::string& handlerError
            )
            {
                return impl_->PrepareDecisionExecute(
                    effect,
                    context,
                    prepared,
                    handlerError
                );
            }
        )
        || !registerHandler(
            "decision.enqueue",
            [this](
                const NativeEffect& effect,
                const NativeEffectExecutionContext& context,
                PreparedNativeEffect& prepared,
                std::string& handlerError
            )
            {
                return impl_->PrepareDispatchEnqueue(
                    effect,
                    context,
                    prepared,
                    handlerError,
                    Impl::DispatchKind::Decision
                );
            }
        )
        || !registerHandler(
            "decision.cancel",
            [this](
                const NativeEffect& effect,
                const NativeEffectExecutionContext&,
                PreparedNativeEffect& prepared,
                std::string& handlerError
            )
            {
                return impl_->PrepareDispatchCancel(
                    effect,
                    prepared,
                    handlerError,
                    Impl::DispatchKind::Decision
                );
            }
        )
        || !registerHandler(
            "queue.cancel",
            [this](
                const NativeEffect& effect,
                const NativeEffectExecutionContext&,
                PreparedNativeEffect& prepared,
                std::string& handlerError
            )
            {
                return impl_->PrepareDispatchCancel(
                    effect,
                    prepared,
                    handlerError,
                    std::nullopt
                );
            }
        )
        || !registerHandler(
            "province.set_owner",
            [this](
                const NativeEffect& effect,
                const NativeEffectExecutionContext& context,
                PreparedNativeEffect& prepared,
                std::string& handlerError
            )
            {
                return impl_->PrepareProvinceCountryAssignment(
                    effect,
                    context,
                    prepared,
                    handlerError,
                    true
                );
            }
        )
        || !registerHandler(
            "province.set_controller",
            [this](
                const NativeEffect& effect,
                const NativeEffectExecutionContext& context,
                PreparedNativeEffect& prepared,
                std::string& handlerError
            )
            {
                return impl_->PrepareProvinceCountryAssignment(
                    effect,
                    context,
                    prepared,
                    handlerError,
                    false
                );
            }
        )
        || !registerHandler(
            "province.add_core",
            [this](
                const NativeEffect& effect,
                const NativeEffectExecutionContext& context,
                PreparedNativeEffect& prepared,
                std::string& handlerError
            )
            {
                return impl_->PrepareProvinceCore(
                    effect,
                    context,
                    prepared,
                    handlerError,
                    true
                );
            }
        )
        || !registerHandler(
            "province.remove_core",
            [this](
                const NativeEffect& effect,
                const NativeEffectExecutionContext& context,
                PreparedNativeEffect& prepared,
                std::string& handlerError
            )
            {
                return impl_->PrepareProvinceCore(
                    effect,
                    context,
                    prepared,
                    handlerError,
                    false
                );
            }
        )
        || !registerHandler(
            "province.set_building_level",
            [this](
                const NativeEffect& effect,
                const NativeEffectExecutionContext& context,
                PreparedNativeEffect& prepared,
                std::string& handlerError
            )
            {
                return impl_->PrepareProvinceBuildingLevel(
                    effect,
                    context,
                    prepared,
                    handlerError
                );
            }
        )
        || !registerHandler(
            "technology.set_level",
            [this](
                const NativeEffect& effect,
                const NativeEffectExecutionContext& context,
                PreparedNativeEffect& prepared,
                std::string& handlerError
            )
            {
                return impl_->PrepareTechnologyLevel(
                    effect,
                    context,
                    prepared,
                    handlerError
                );
            }
        )
        || !registerHandler(
            "research.set_progress",
            [this](
                const NativeEffect& effect,
                const NativeEffectExecutionContext& context,
                PreparedNativeEffect& prepared,
                std::string& handlerError
            )
            {
                return impl_->PrepareResearchProgress(
                    effect,
                    context,
                    prepared,
                    handlerError
                );
            }
        )
        || !registerHandler(
            "research.complete",
            [this](
                const NativeEffect& effect,
                const NativeEffectExecutionContext& context,
                PreparedNativeEffect& prepared,
                std::string& handlerError
            )
            {
                return impl_->PrepareResearchComplete(
                    effect,
                    context,
                    prepared,
                    handlerError
                );
            }
        )
        || !registerHandler(
            "research.cancel",
            [this](
                const NativeEffect& effect,
                const NativeEffectExecutionContext& context,
                PreparedNativeEffect& prepared,
                std::string& handlerError
            )
            {
                return impl_->PrepareResearchCancel(
                    effect,
                    context,
                    prepared,
                    handlerError
                );
            }
        )
        || !registerHandler(
            "country.set_capital",
            [this](
                const NativeEffect& effect,
                const NativeEffectExecutionContext& context,
                PreparedNativeEffect& prepared,
                std::string& handlerError
            )
            {
                return impl_->PrepareCountryCapital(
                    effect,
                    context,
                    prepared,
                    handlerError,
                    false
                );
            }
        )
        || !registerHandler(
            "country.set_acting_capital",
            [this](
                const NativeEffect& effect,
                const NativeEffectExecutionContext& context,
                PreparedNativeEffect& prepared,
                std::string& handlerError
            )
            {
                return impl_->PrepareCountryCapital(
                    effect,
                    context,
                    prepared,
                    handlerError,
                    true
                );
            }
        )
        || !registerHandler(
            "country.add_manpower",
            scalarHandler(CountryScalarKind::Manpower, false)
        )
        || !registerHandler(
            "country.set_manpower",
            scalarHandler(CountryScalarKind::Manpower, true)
        )
        || !registerHandler("country.add_goods", goodsHandler(false))
        || !registerHandler("country.set_goods", goodsHandler(true))
        || !registerHandler(
            "country.add_national_unity",
            nationalUnityHandler(false)
        )
        || !registerHandler(
            "country.set_national_unity",
            nationalUnityHandler(true)
        )
        || !registerHandler(
            "country.add_dissent",
            scalarHandler(CountryScalarKind::Dissent, false)
        )
        || !registerHandler(
            "country.set_dissent",
            scalarHandler(CountryScalarKind::Dissent, true)
        )
        || !registerHandler(
            "country.add_neutrality",
            scalarHandler(CountryScalarKind::Neutrality, false)
        )
        || !registerHandler(
            "country.set_neutrality",
            scalarHandler(CountryScalarKind::Neutrality, true)
        )
        || !registerHandler(
            "country.add_officers",
            scalarHandler(CountryScalarKind::Officers, false)
        )
        || !registerHandler(
            "country.set_officers",
            scalarHandler(CountryScalarKind::Officers, true)
        )
        || !registerHandler(
            "country.add_diplomatic_influence",
            diplomaticInfluenceHandler(false)
        )
        || !registerHandler(
            "country.set_diplomatic_influence",
            diplomaticInfluenceHandler(true)
        )
        || !registerHandler(
            "country.add_leadership",
            leadershipHandler(false)
        )
        || !registerHandler(
            "country.set_leadership",
            leadershipHandler(true)
        )
        || !registerHandler(
            "country.add_convoys",
            directScalarHandler(DirectCountryScalarKind::Convoys, false)
        )
        || !registerHandler(
            "country.set_convoys",
            directScalarHandler(DirectCountryScalarKind::Convoys, true)
        )
        || !registerHandler(
            "country.add_escorts",
            directScalarHandler(DirectCountryScalarKind::Escorts, false)
        )
        || !registerHandler(
            "country.set_escorts",
            directScalarHandler(DirectCountryScalarKind::Escorts, true)
        )
        || !registerHandler(
            "country.add_free_spies",
            directScalarHandler(DirectCountryScalarKind::FreeSpies, false)
        )
        || !registerHandler(
            "country.set_free_spies",
            directScalarHandler(DirectCountryScalarKind::FreeSpies, true)
        )
        || !registerHandler(
            "country.set_government",
            definitionHandler(NamedCountryDefinitionKind::Government)
        )
        || !registerHandler(
            "country.set_ruling_ideology",
            definitionHandler(NamedCountryDefinitionKind::RulingIdeology)
        )
        || !registerHandler(
            "country.add_ideology_popularity",
            ideologyValueHandler(IdeologyValueKind::Popularity, false)
        )
        || !registerHandler(
            "country.set_ideology_popularity",
            ideologyValueHandler(IdeologyValueKind::Popularity, true)
        )
        || !registerHandler(
            "country.add_ideology_organization",
            ideologyValueHandler(IdeologyValueKind::Organization, false)
        )
        || !registerHandler(
            "country.set_ideology_organization",
            ideologyValueHandler(IdeologyValueKind::Organization, true)
        )
        || !registerHandler(
            "diplomacy.add_relation",
            relationHandler(false)
        )
        || !registerHandler(
            "diplomacy.set_relation",
            relationHandler(true)
        )
        || !registerHandler(
            "diplomacy.add_threat",
            threatHandler(false)
        )
        || !registerHandler(
            "diplomacy.set_threat",
            threatHandler(true)
        )
        || !registerHandler(
            "espionage.set_presence_level",
            [this](
                const NativeEffect& effect,
                const NativeEffectExecutionContext& context,
                PreparedNativeEffect& prepared,
                std::string& handlerError
            )
            {
                return impl_->PrepareSpyPresence(
                    effect,
                    context,
                    prepared,
                    handlerError
                );
            }
        )
        || !registerHandler(
            "intelligence.set_province_level",
            [this](
                const NativeEffect& effect,
                const NativeEffectExecutionContext& context,
                PreparedNativeEffect& prepared,
                std::string& handlerError
            )
            {
                return impl_->PrepareProvinceIntel(
                    effect,
                    context,
                    prepared,
                    handlerError
                );
            }
        )
        || !registerHandler(
            "country.add_modifier",
            countryModifierHandler(true)
        )
        || !registerHandler(
            "country.remove_modifier",
            countryModifierHandler(false)
        )
        || !registerHandler(
            "province.add_modifier",
            [this](
                const NativeEffect& effect,
                const NativeEffectExecutionContext& context,
                PreparedNativeEffect& prepared,
                std::string& handlerError
            )
            {
                return impl_->PrepareProvinceAddModifier(
                    effect,
                    context,
                    prepared,
                    handlerError
                );
            }
        )
        || !registerHandler(
            "province.remove_modifier",
            [this](
                const NativeEffect& effect,
                const NativeEffectExecutionContext& context,
                PreparedNativeEffect& prepared,
                std::string& handlerError
            )
            {
                return impl_->PrepareProvinceRemoveModifier(
                    effect,
                    context,
                    prepared,
                    handlerError
                );
            }
        ))
    {
        return false;
    }
    impl_->service = &service;
    error.clear();
    return true;
}

void Hoi3GameplayEffects::UnregisterHandlers()
{
    if (!impl_ || !impl_->service)
    {
        return;
    }
    for (auto operation = impl_->registeredOperations.rbegin();
         operation != impl_->registeredOperations.rend();
         ++operation)
    {
        impl_->service->UnregisterHandler(*operation);
    }
    impl_->ClearDispatchQueue();
    impl_->registeredOperations.clear();
    impl_->service = nullptr;
}

std::vector<std::string> Hoi3GameplayEffects::Tick()
{
    return impl_ ? impl_->TickDispatchQueue() : std::vector<std::string>{};
}

std::size_t Hoi3GameplayEffects::ClearQueuedActions()
{
    return impl_ ? impl_->ClearDispatchQueue() : 0;
}

bool Hoi3GameplayEffects::IsSupportedExecutable() const
{
    return impl_ && impl_->supported;
}

}
