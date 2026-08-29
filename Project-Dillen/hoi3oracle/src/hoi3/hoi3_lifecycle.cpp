#include "hoi3_lifecycle.h"

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <utility>

#include "engine_registry.h"

namespace
{

constexpr std::size_t PlayerTagStorageSize = 4;
constexpr uint64_t FingerprintOffsetBasis = 1469598103934665603ULL;
constexpr uint64_t FingerprintPrime = 1099511628211ULL;

bool TryCopyMemory(
    const void* source,
    void* destination,
    std::size_t size
) noexcept
{
#if defined(_MSC_VER)
    __try
    {
        std::memcpy(destination, source, size);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    std::memcpy(destination, source, size);
    return true;
#endif
}

bool IsTagCharacter(uint8_t character)
{
    return (character >= 'A' && character <= 'Z')
        || (character >= '0' && character <= '9')
        || character == '-';
}

void HashValue(uint64_t& hash, std::uintptr_t value)
{
    for (std::size_t index = 0; index < sizeof(value); ++index)
    {
        hash ^= static_cast<uint8_t>(value >> (index * 8));
        hash *= FingerprintPrime;
    }
}

bool TryReadCurrentTotalDays(
    core::engine::EngineRegistry& registry,
    std::uintptr_t gameState,
    int32_t& totalDays
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    using Function = int32_t (__thiscall*)(void*);
    const std::uintptr_t function = registry.Resolve(
        core::engine::SymbolId::CurrentDateTotalDays
    );
    const std::uintptr_t date = gameState + registry.FieldValue(
        core::engine::FieldId::CurrentDateOffset
    );
    if (!function || !date)
    {
        return false;
    }
    __try
    {
        totalDays = reinterpret_cast<Function>(function)(
            reinterpret_cast<void*>(date)
        );
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        totalDays = 0;
        return false;
    }
#else
    (void)registry;
    (void)gameState;
    totalDays = 0;
    return false;
#endif
}

uint64_t BuildWorldFingerprint(
    core::engine::EngineRegistry& registry,
    std::uintptr_t gameState
) noexcept
{
    uint64_t hash = FingerprintOffsetBasis;
    HashValue(hash, gameState);

    std::uintptr_t countryDatabase = 0;
    const std::uintptr_t countryDatabaseSymbol = registry.Resolve(
        core::engine::SymbolId::CountryDatabaseSingleton
    );
    if (countryDatabaseSymbol)
    {
        TryCopyMemory(
            reinterpret_cast<const void*>(countryDatabaseSymbol),
            &countryDatabase,
            sizeof(countryDatabase)
        );
    }
    HashValue(hash, countryDatabase);

    std::uintptr_t countryTable = 0;
    if (countryDatabase)
    {
        TryCopyMemory(
            reinterpret_cast<const void*>(
                countryDatabase + registry.FieldValue(
                    core::engine::FieldId::CountryPointerTableOffset
                )
            ),
            &countryTable,
            sizeof(countryTable)
        );
    }
    HashValue(hash, countryTable);

    int32_t playerCountryIndex = -1;
    TryCopyMemory(
        reinterpret_cast<const void*>(
            gameState + registry.FieldValue(
                core::engine::FieldId::PlayerCountryIndexOffset
            )
        ),
        &playerCountryIndex,
        sizeof(playerCountryIndex)
    );
    HashValue(hash, static_cast<std::uintptr_t>(playerCountryIndex));
    if (countryTable && playerCountryIndex >= 0
        && playerCountryIndex < 1024)
    {
        std::uintptr_t playerCountry = 0;
        TryCopyMemory(
            reinterpret_cast<const void*>(
                countryTable
                + static_cast<std::uintptr_t>(playerCountryIndex)
                    * sizeof(std::uintptr_t)
            ),
            &playerCountry,
            sizeof(playerCountry)
        );
        HashValue(hash, playerCountry);
    }

    std::uintptr_t provinceBegin = 0;
    std::uintptr_t provinceEnd = 0;
    TryCopyMemory(
        reinterpret_cast<const void*>(
            gameState + registry.FieldValue(
                core::engine::FieldId::ProvinceVectorBeginOffset
            )
        ),
        &provinceBegin,
        sizeof(provinceBegin)
    );
    TryCopyMemory(
        reinterpret_cast<const void*>(
            gameState + registry.FieldValue(
                core::engine::FieldId::ProvinceVectorEndOffset
            )
        ),
        &provinceEnd,
        sizeof(provinceEnd)
    );
    HashValue(hash, provinceBegin);
    HashValue(hash, provinceEnd);
    return hash;
}

}

bool DecodeHoi3PlayerTag(
    const uint8_t* bytes,
    std::size_t size,
    std::string& playerTag
)
{
    playerTag.clear();
    if (!bytes
        || size < PlayerTagStorageSize
        || bytes[3] != 0
        || !IsTagCharacter(bytes[0])
        || !IsTagCharacter(bytes[1])
        || !IsTagCharacter(bytes[2]))
    {
        return false;
    }
    playerTag.assign(
        reinterpret_cast<const char*>(bytes),
        3
    );
    return true;
}

Hoi3LifecycleProbeResult ProbeHoi3Lifecycle()
{
    auto& registry = core::engine::GetEngineRegistry();
    std::string registryError;
    if (!registry.InitializeCurrentProcess(registryError))
    {
        Hoi3LifecycleProbeResult result;
        result.status =
            Hoi3LifecycleProbeStatus::UnsupportedExecutable;
        return result;
    }

    std::uintptr_t gameState = 0;
    if (!TryCopyMemory(
            reinterpret_cast<const void*>(registry.Resolve(
                core::engine::SymbolId::GameStateSingleton
            )),
            &gameState,
            sizeof(gameState)
        ))
    {
        Hoi3LifecycleProbeResult result;
        result.status = Hoi3LifecycleProbeStatus::Unavailable;
        return result;
    }
    if (gameState == 0)
    {
        Hoi3LifecycleProbeResult result;
        result.status = Hoi3LifecycleProbeStatus::Frontend;
        result.playerTag = "---";
        return result;
    }

    uint8_t tagBytes[PlayerTagStorageSize]{};
    if (!TryCopyMemory(
            reinterpret_cast<const void*>(
                gameState + registry.FieldValue(
                    core::engine::FieldId::GameStatePlayerTagOffset
                )
            ),
            tagBytes,
            sizeof(tagBytes)
        ))
    {
        Hoi3LifecycleProbeResult result;
        result.status = Hoi3LifecycleProbeStatus::Unavailable;
        return result;
    }

    std::string playerTag;
    if (!DecodeHoi3PlayerTag(
            tagBytes,
            sizeof(tagBytes),
            playerTag
        ))
    {
        Hoi3LifecycleProbeResult result;
        result.status = Hoi3LifecycleProbeStatus::Unavailable;
        return result;
    }
    Hoi3LifecycleProbeResult result;
    result.status = playerTag == "---"
        ? Hoi3LifecycleProbeStatus::Frontend
        : Hoi3LifecycleProbeStatus::Gameplay;
    result.playerTag = std::move(playerTag);
    result.gameStateAddress = gameState;
    if (result.status == Hoi3LifecycleProbeStatus::Gameplay)
    {
        result.worldFingerprint = BuildWorldFingerprint(
            registry,
            gameState
        );
        result.hasTotalDays = TryReadCurrentTotalDays(
            registry,
            gameState,
            result.totalDays
        );
    }
    return result;
}
