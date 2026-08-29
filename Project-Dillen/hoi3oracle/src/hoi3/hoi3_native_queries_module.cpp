#include "hoi3_native_queries_module.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>

#include "engine_abi_hoi3_tfh_402.h"
#include "engine_registry.h"
#include "hoi3_native_object_keys.h"
#include "native_object_resolver.h"
#include "native_query_service.h"

namespace core
{
namespace
{

constexpr std::string_view Provider = "hoi3_native_queries";
constexpr uint64_t SingletonStableId = 1;
constexpr std::size_t MaximumCountryCount = 4096;
constexpr std::size_t MaximumProvinceCount = 65536;
constexpr std::size_t MaximumUnitNodes = 65536;
constexpr std::size_t MaximumLeaderNodes = 8192;
constexpr std::string_view SameGenerationSnapshotProbeId =
    "hoi3.query.same_generation";
constexpr std::string_view UnitLeaderGenerationProbeId =
    "hoi3.objects.unit_leader_generation";

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

bool TryReadCurrentDateTotalDays(
    std::uintptr_t function,
    std::uintptr_t date,
    int32_t& output
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    using Function = int32_t (__thiscall*)(void*);
    if (!function || !date)
    {
        return false;
    }

    __try
    {
        output = reinterpret_cast<Function>(function)(
            reinterpret_cast<void*>(date)
        );
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
#else
    (void)function;
    (void)date;
    (void)output;
    return false;
#endif
}

bool ResolveSingleton(
    engine::EngineRegistry& engine,
    engine::SymbolId symbol,
    std::uintptr_t& address,
    std::string& error
) noexcept
{
    address = 0;
    const std::uintptr_t storage = engine.Resolve(symbol);
    if (!storage || !TryRead(storage, address) || !address)
    {
        error = "hoi3_singleton_unavailable";
        return false;
    }
    return true;
}

bool ResolveGameState(
    engine::EngineRegistry& engine,
    std::uintptr_t& address,
    std::string& error
) noexcept
{
    return ResolveSingleton(
        engine,
        engine::SymbolId::GameStateSingleton,
        address,
        error
    );
}

bool ResolveCountryDatabase(
    engine::EngineRegistry& engine,
    std::uintptr_t& address,
    std::string& error
) noexcept
{
    return ResolveSingleton(
        engine,
        engine::SymbolId::CountryDatabaseSingleton,
        address,
        error
    );
}

bool ReadCountryTable(
    engine::EngineRegistry& engine,
    std::uintptr_t& table,
    std::string& error
) noexcept
{
    std::uintptr_t database = 0;
    table = 0;
    if (!ResolveCountryDatabase(engine, database, error)
        || !TryRead(
            database + engine.FieldValue(
                engine::FieldId::CountryPointerTableOffset
            ),
            table
        )
        || !table)
    {
        error = "hoi3_country_table_unavailable";
        return false;
    }
    return true;
}

bool ReadCountryIdentity(
    engine::EngineRegistry& engine,
    std::uintptr_t country,
    engine::abi::CountryTagValue& identity
) noexcept
{
    return TryRead(
            country + engine.FieldValue(engine::FieldId::CountryTagOffset),
            identity
        )
        && identity.index < MaximumCountryCount;
}

bool ResolveCountryByStableTag(
    engine::EngineRegistry& engine,
    uint64_t stableId,
    std::uintptr_t& country,
    std::string& error
) noexcept
{
    country = 0;
    std::string expectedTag;
    std::uintptr_t table = 0;
    if (!UnpackHoi3CountryTag(stableId, expectedTag)
        || !ReadCountryTable(engine, table, error))
    {
        if (error.empty())
        {
            error = "hoi3_country_tag_invalid";
        }
        return false;
    }
    const uint32_t expected = static_cast<uint32_t>(stableId & 0x00FFFFFFu);
    for (std::size_t index = 0; index < MaximumCountryCount; ++index)
    {
        std::uintptr_t candidate = 0;
        if (!TryRead(
                table + index * sizeof(uint32_t),
                candidate
            )
            || !candidate)
        {
            continue;
        }
        engine::abi::CountryTagValue identity{};
        if (ReadCountryIdentity(engine, candidate, identity)
            && (identity.tag & 0x00FFFFFFu) == expected
            && identity.index == index)
        {
            country = candidate;
            return true;
        }
    }
    error = "hoi3_country_not_found: " + expectedTag;
    return false;
}

bool ResolveProvinceById(
    engine::EngineRegistry& engine,
    uint64_t stableId,
    std::uintptr_t& province,
    std::string& error
) noexcept
{
    province = 0;
    std::uintptr_t gameState = 0;
    std::uintptr_t begin = 0;
    std::uintptr_t end = 0;
    if (stableId == 0
        || stableId >= MaximumProvinceCount
        || !ResolveGameState(engine, gameState, error)
        || !TryRead(
            gameState + engine.FieldValue(
                engine::FieldId::ProvinceVectorBeginOffset
            ),
            begin
        )
        || !TryRead(
            gameState + engine.FieldValue(
                engine::FieldId::ProvinceVectorEndOffset
            ),
            end
        )
        || !begin
        || end <= begin
        || (end - begin) % sizeof(uint32_t) != 0)
    {
        error = "hoi3_province_table_unavailable";
        return false;
    }
    const uint64_t count = (end - begin) / sizeof(uint32_t);
    if (stableId >= count
        || !TryRead(
            begin + stableId * sizeof(uint32_t),
            province
        )
        || !province)
    {
        error = "hoi3_province_not_found";
        return false;
    }
    return true;
}

std::uintptr_t InvokeTechnologyLookup(
    std::uintptr_t lookup,
    const char* name
) noexcept
{
#if defined(_MSC_VER) && defined(_M_IX86)
    using Function = void* (__cdecl*)(const char*);
    std::uintptr_t definition = 0;
    __try
    {
        definition = reinterpret_cast<std::uintptr_t>(
            reinterpret_cast<Function>(lookup)(name)
        );
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        definition = 0;
    }
    return definition;
#else
    (void)lookup;
    (void)name;
    return 0;
#endif
}

bool ResolveTechnologyDefinitionByName(
    engine::EngineRegistry& engine,
    std::string name,
    std::uintptr_t& definition,
    std::string& error
) noexcept
{
    definition = 0;
    name = NormalizeHoi3DefinitionName(name);
    const std::uintptr_t lookup = engine.Resolve(
        engine::SymbolId::TechnologyLookup
    );
    if (name.empty() || !lookup)
    {
        error = "hoi3_technology_name_invalid";
        return false;
    }
    definition = InvokeTechnologyLookup(lookup, name.c_str());
    if (!definition)
    {
        error = "hoi3_technology_definition_not_found: " + name;
        return false;
    }
    return true;
}

bool ResolveTechnologyStatusByCountry(
    engine::EngineRegistry& engine,
    uint64_t countryStableId,
    std::uintptr_t& status,
    std::string& error
) noexcept
{
    std::uintptr_t country = 0;
    status = 0;
    if (!ResolveCountryByStableTag(
            engine,
            countryStableId,
            country,
            error
        )
        || !TryRead(
            country + engine.FieldValue(
                engine::FieldId::CountryTechnologyModifierOffset
            ),
            status
        )
        || !status)
    {
        if (error.empty())
        {
            error = "hoi3_technology_status_unavailable";
        }
        status = 0;
        return false;
    }
    return true;
}

bool ResolveRelationByStableTags(
    engine::EngineRegistry& engine,
    uint64_t stableId,
    std::uintptr_t& relation,
    std::string& error
) noexcept
{
    relation = 0;
    std::string sourceTag;
    std::string targetTag;
    uint64_t sourceStableId = 0;
    uint64_t targetStableId = 0;
    std::uintptr_t source = 0;
    std::uintptr_t target = 0;
    std::uintptr_t table = 0;
    engine::abi::CountryTagValue targetIdentity{};
    if (!UnpackHoi3RelationKey(stableId, sourceTag, targetTag)
        || !PackHoi3CountryTag(sourceTag, sourceStableId)
        || !PackHoi3CountryTag(targetTag, targetStableId)
        || !ResolveCountryByStableTag(
            engine, sourceStableId, source, error
        )
        || !ResolveCountryByStableTag(
            engine, targetStableId, target, error
        )
        || !ReadCountryIdentity(engine, target, targetIdentity)
        || !TryRead(
            source + engine.FieldValue(
                engine::FieldId::CountryRelationTableOffset
            ),
            table
        )
        || !table
        || !TryRead(
            table + static_cast<std::uintptr_t>(targetIdentity.index)
                * sizeof(uint32_t),
            relation
        )
        || !relation)
    {
        if (error.empty())
        {
            error = "hoi3_relation_not_found";
        }
        relation = 0;
        return false;
    }
    return true;
}

bool UnitIdentityMatches(
    engine::EngineRegistry& engine,
    std::uintptr_t unit,
    uint32_t expectedId0,
    uint32_t expectedId1,
    uint32_t expectedCountryIndex
) noexcept
{
    uint32_t id0 = 0;
    uint32_t id1 = 0;
    uint32_t countryIndex = 0;
    return unit
        && TryRead(
            unit + engine.FieldValue(engine::FieldId::UnitId0Offset),
            id0
        )
        && TryRead(
            unit + engine.FieldValue(engine::FieldId::UnitId1Offset),
            id1
        )
        && TryRead(
            unit + engine.FieldValue(
                engine::FieldId::UnitCountryIdOffset
            ),
            countryIndex
        )
        && id0 == expectedId0
        && id1 == expectedId1
        && countryIndex == expectedCountryIndex;
}

bool ScanCountryForUnit(
    engine::EngineRegistry& engine,
    std::uintptr_t country,
    uint32_t countryIndex,
    uint32_t expectedId0,
    uint32_t expectedId1,
    std::uintptr_t& unit
) noexcept
{
    std::uintptr_t node = 0;
    std::uintptr_t tail = 0;
    uint32_t count = 0;
    unit = 0;
    if (!TryRead(
            country + engine.FieldValue(
                engine::FieldId::CountryUnitListHeadOffset
            ),
            node
        )
        || !TryRead(
            country + engine.FieldValue(
                engine::FieldId::CountryUnitListTailOffset
            ),
            tail
        )
        || !TryRead(
            country + engine.FieldValue(
                engine::FieldId::CountryUnitListCountOffset
            ),
            count
        )
        || count > MaximumUnitNodes
        || ((count == 0) != (node == 0))
        || ((count == 0) != (tail == 0)))
    {
        return false;
    }
    std::uintptr_t previous = 0;
    std::size_t visited = 0;
    while (node && visited < count)
    {
        std::uintptr_t candidateUnit = 0;
        std::uintptr_t nodePrevious = 0;
        std::uintptr_t next = 0;
        if (!TryRead(
                node + engine.FieldValue(
                    engine::FieldId::UnitListNodeUnitOffset
                ),
                candidateUnit
            )
            || !TryRead(
                node + engine.FieldValue(
                    engine::FieldId::UnitListNodePreviousOffset
                ),
                nodePrevious
            )
            || !TryRead(
                node + engine.FieldValue(
                    engine::FieldId::UnitListNodeNextOffset
                ),
                next
            )
            || nodePrevious != previous)
        {
            return false;
        }
        if (UnitIdentityMatches(
                engine,
                candidateUnit,
                expectedId0,
                expectedId1,
                countryIndex
            ))
        {
            unit = candidateUnit;
            return true;
        }
        if (next == node)
        {
            return false;
        }
        previous = node;
        node = next;
        ++visited;
    }
    return node == 0 && visited == count && previous == tail;
}

bool ResolveUnitByNativeId(
    engine::EngineRegistry& engine,
    uint64_t stableId,
    std::string ownerTag,
    std::uintptr_t& unit,
    std::string& error
) noexcept
{
    uint32_t expectedId0 = 0;
    uint32_t expectedId1 = 0;
    std::uintptr_t table = 0;
    unit = 0;
    if (!UnpackHoi3UnitKey(stableId, expectedId0, expectedId1)
        || !ReadCountryTable(engine, table, error))
    {
        if (error.empty())
        {
            error = "hoi3_unit_key_invalid";
        }
        return false;
    }

    uint64_t ownerStableId = 0;
    std::uintptr_t ownerCountry = 0;
    if (!ownerTag.empty())
    {
        if (!PackHoi3CountryTag(ownerTag, ownerStableId)
            || !ResolveCountryByStableTag(
                engine,
                ownerStableId,
                ownerCountry,
                error
            ))
        {
            return false;
        }
        engine::abi::CountryTagValue identity{};
        if (!ReadCountryIdentity(engine, ownerCountry, identity)
            || !ScanCountryForUnit(
                engine,
                ownerCountry,
                identity.index,
                expectedId0,
                expectedId1,
                unit
            ))
        {
            error = "hoi3_country_unit_registry_unreadable";
            return false;
        }
        if (unit)
        {
            return true;
        }
    }
    else
    {
        for (std::size_t index = 0;
             index < MaximumCountryCount;
             ++index)
        {
            std::uintptr_t country = 0;
            if (!TryRead(table + index * sizeof(uint32_t), country)
                || !country)
            {
                continue;
            }
            if (!ScanCountryForUnit(
                    engine,
                    country,
                    static_cast<uint32_t>(index),
                    expectedId0,
                    expectedId1,
                    unit
                ))
            {
                error = "hoi3_country_unit_registry_unreadable";
                return false;
            }
            if (unit)
            {
                return true;
            }
        }
    }
    error = "hoi3_unit_not_found";
    return false;
}

bool ScanLeaderRegistry(
    engine::EngineRegistry& engine,
    std::uintptr_t node,
    uint32_t expectedId0,
    uint32_t expectedId1,
    std::uintptr_t& leader
) noexcept
{
    for (std::size_t guard = 0;
         node && guard < MaximumLeaderNodes;
         ++guard)
    {
        std::uintptr_t candidate = 0;
        std::uintptr_t next = 0;
        uint32_t id0 = 0;
        uint32_t id1 = 0;
        if (!TryRead(
                node + engine.FieldValue(
                    engine::FieldId::LeaderRegistryNodeLeaderOffset
                ),
                candidate
            )
            || !TryRead(
                node + engine.FieldValue(
                    engine::FieldId::LeaderRegistryNodeNextOffset
                ),
                next
            ))
        {
            return false;
        }
        if (candidate
            && TryRead(
                candidate + engine.FieldValue(
                    engine::FieldId::LeaderObjectId0Offset
                ),
                id0
            )
            && TryRead(
                candidate + engine.FieldValue(
                    engine::FieldId::LeaderObjectId1Offset
                ),
                id1
            )
            && id0 == expectedId0
            && id1 == expectedId1)
        {
            leader = candidate;
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

bool ScanCountryForLeader(
    engine::EngineRegistry& engine,
    std::uintptr_t country,
    uint32_t expectedId0,
    uint32_t expectedId1,
    std::uintptr_t& leader
) noexcept
{
    const engine::FieldId registries[] = {
        engine::FieldId::CountryLeaderRegistryOffset,
        engine::FieldId::CountryReserveLeaderRegistryOffset
    };
    leader = 0;
    for (const engine::FieldId registry : registries)
    {
        std::uintptr_t node = 0;
        if (!TryRead(country + engine.FieldValue(registry), node)
            || !ScanLeaderRegistry(
                engine,
                node,
                expectedId0,
                expectedId1,
                leader
            ))
        {
            return false;
        }
        if (leader)
        {
            return true;
        }
    }
    return true;
}

bool ResolveLeaderByNativeId(
    engine::EngineRegistry& engine,
    uint64_t stableId,
    std::string ownerTag,
    std::uintptr_t& leader,
    std::string& error
) noexcept
{
    leader = 0;
    uint32_t expectedId0 = 0;
    uint32_t expectedId1 = 0;
    if (!UnpackHoi3LeaderKey(stableId, expectedId0, expectedId1))
    {
        error = "hoi3_leader_native_id_invalid";
        return false;
    }
    std::uintptr_t table = 0;
    if (!ReadCountryTable(engine, table, error))
    {
        return false;
    }
    if (!ownerTag.empty())
    {
        uint64_t ownerStableId = 0;
        std::uintptr_t ownerCountry = 0;
        if (!PackHoi3CountryTag(ownerTag, ownerStableId)
            || !ResolveCountryByStableTag(
                engine,
                ownerStableId,
                ownerCountry,
                error
            )
            || !ScanCountryForLeader(
                engine,
                ownerCountry,
                expectedId0,
                expectedId1,
                leader
            ))
        {
            if (error.empty())
            {
                error = "hoi3_country_leader_registry_unreadable";
            }
            return false;
        }
        if (leader)
        {
            return true;
        }
    }
    else
    {
        for (std::size_t index = 0;
             index < MaximumCountryCount;
             ++index)
        {
            std::uintptr_t country = 0;
            if (!TryRead(table + index * sizeof(uint32_t), country)
                || !country)
            {
                continue;
            }
            if (!ScanCountryForLeader(
                engine,
                country,
                expectedId0,
                expectedId1,
                leader
            ))
            {
                error = "hoi3_country_leader_registry_unreadable";
                return false;
            }
            if (leader)
            {
                return true;
            }
        }
    }
    error = "hoi3_leader_not_found";
    return false;
}

bool ResolveGuarded(
    NativeObjectResolverService& resolvers,
    const NativeQueryExecutionContext& context,
    engine::TypeId type,
    uint64_t stableId,
    engine::ObjectHandle& handle,
    std::string& error,
    std::string stableName = {}
)
{
    return resolvers.ResolveGuarded(
        {type, stableId, std::move(stableName)},
        context.safetyLease,
        handle,
        error
    );
}

bool ReadRequestedTag(
    const NativeQueryRequest& request,
    const NativeQueryExecutionContext& context,
    uint64_t& stableId,
    std::string& error
)
{
    std::string tag = context.playerTag;
    if (const NativeQueryValue* value = request.Find("tag"))
    {
        if (!NativeQueryValueToString(*value, tag))
        {
            error = "country_tag_invalid";
            return false;
        }
    }
    if (!PackHoi3CountryTag(tag, stableId))
    {
        error = "country_tag_invalid";
        return false;
    }
    return true;
}

bool ReadProvinceId(
    const NativeQueryRequest& request,
    uint64_t& provinceId,
    std::string& error
)
{
    int64_t value = 0;
    const NativeQueryValue* argument = request.Find("province_id");
    if (!argument
        || !NativeQueryValueToInteger(*argument, value)
        || value <= 0
        || value >= static_cast<int64_t>(MaximumProvinceCount))
    {
        error = "province_id_invalid";
        return false;
    }
    provinceId = static_cast<uint64_t>(value);
    return true;
}

bool ReadTargetTag(
    const NativeQueryRequest& request,
    std::string& tag,
    std::string& error
)
{
    const NativeQueryValue* value = request.Find("target_tag");
    uint64_t stableId = 0;
    if (!value
        || !NativeQueryValueToString(*value, tag)
        || !PackHoi3CountryTag(tag, stableId)
        || !UnpackHoi3CountryTag(stableId, tag))
    {
        error = "diplomacy_target_tag_invalid";
        return false;
    }
    return true;
}

bool ReadTechnologyName(
    const NativeQueryRequest& request,
    std::string& name,
    std::string& error
)
{
    const NativeQueryValue* value = request.Find("technology");
    if (!value)
    {
        value = request.Find("name");
    }
    if (!value || !NativeQueryValueToString(*value, name))
    {
        error = "technology_name_invalid";
        return false;
    }
    name = NormalizeHoi3DefinitionName(name);
    if (name.empty())
    {
        error = "technology_name_invalid";
        return false;
    }
    return true;
}

bool ReadUnitKey(
    const NativeQueryRequest& request,
    uint64_t& stableId,
    std::string& error
)
{
    const NativeQueryValue* first = request.Find("unit_id0");
    const NativeQueryValue* second = request.Find("unit_id1");
    if (!first)
    {
        first = request.Find("id0");
    }
    if (!second)
    {
        second = request.Find("id1");
    }
    int64_t id0 = 0;
    int64_t id1 = 0;
    if (!first
        || !second
        || !NativeQueryValueToInteger(*first, id0)
        || !NativeQueryValueToInteger(*second, id1)
        || id0 < 0
        || id1 < 0
        || id0 > std::numeric_limits<uint32_t>::max()
        || id1 > std::numeric_limits<uint32_t>::max()
        || !PackHoi3UnitKey(
            static_cast<uint32_t>(id0),
            static_cast<uint32_t>(id1),
            stableId
        ))
    {
        error = "unit_stable_id_invalid";
        return false;
    }
    return true;
}

bool ReadLeaderKey(
    const NativeQueryRequest& request,
    uint64_t& stableId,
    std::string& error
)
{
    const NativeQueryValue* first = request.Find("leader_id0");
    const NativeQueryValue* second = request.Find("leader_id1");
    if (!first)
    {
        first = request.Find("id0");
    }
    if (!second)
    {
        second = request.Find("id1");
    }
    int64_t id0 = 0;
    int64_t id1 = 0;
    if (!first
        || !second
        || !NativeQueryValueToInteger(*first, id0)
        || !NativeQueryValueToInteger(*second, id1)
        || id0 < 0
        || id1 < 0
        || id0 > std::numeric_limits<uint32_t>::max()
        || id1 > std::numeric_limits<uint32_t>::max()
        || !PackHoi3LeaderKey(
            static_cast<uint32_t>(id0),
            static_cast<uint32_t>(id1),
            stableId
        ))
    {
        error = "leader_stable_id_invalid";
        return false;
    }
    return true;
}

bool ReadOwnerTag(
    const NativeQueryRequest& request,
    const NativeQueryExecutionContext& context,
    std::string& tag,
    std::string& error
)
{
    uint64_t stableId = 0;
    if (!ReadRequestedTag(request, context, stableId, error)
        || !UnpackHoi3CountryTag(stableId, tag))
    {
        if (error.empty())
        {
            error = "country_tag_invalid";
        }
        return false;
    }
    return true;
}

bool IsTechnologyBeingResearched(
    engine::EngineRegistry& engine,
    std::uintptr_t country,
    std::uintptr_t technology,
    bool& researching,
    std::string& error
) noexcept
{
    researching = false;
    std::uintptr_t node = 0;
    int32_t count = 0;
    if (!TryRead(
            country + engine.FieldValue(
                engine::FieldId::CountryResearchListHeadOffset
            ),
            node
        )
        || !TryRead(
            country + engine.FieldValue(
                engine::FieldId::CountryResearchListCountOffset
            ),
            count
        )
        || count < 0
        || count > static_cast<int32_t>(MaximumProvinceCount))
    {
        error = "hoi3_research_list_unavailable";
        return false;
    }
    for (int32_t index = 0; node && index < count; ++index)
    {
        std::uintptr_t current = 0;
        std::uintptr_t next = 0;
        if (!TryRead(
                node + engine.FieldValue(
                    engine::FieldId::ResearchNodeTechnologyOffset
                ),
                current
            )
            || !TryRead(
                node + engine.FieldValue(
                    engine::FieldId::ResearchNodeNextOffset
                ),
                next
            ))
        {
            error = "hoi3_research_list_unavailable";
            return false;
        }
        if (current == technology)
        {
            researching = true;
            return true;
        }
        if (next == node)
        {
            error = "hoi3_research_list_cycle";
            return false;
        }
        node = next;
    }
    return true;
}

bool CountryTagForIndex(
    engine::EngineRegistry& engine,
    uint32_t index,
    std::string& tag
) noexcept
{
    tag.clear();
    if (index >= MaximumCountryCount)
    {
        return false;
    }
    std::uintptr_t table = 0;
    std::string error;
    std::uintptr_t country = 0;
    engine::abi::CountryTagValue identity{};
    return ReadCountryTable(engine, table, error)
        && TryRead(table + index * sizeof(uint32_t), country)
        && country
        && ReadCountryIdentity(engine, country, identity)
        && identity.index == index
        && UnpackHoi3CountryTag(identity.tag, tag);
}

struct UnitLeaderRegistryInspection
{
    uint32_t countryIndex = 0;
    uint32_t unitCount = 0;
    uint32_t assignedUnitCount = 0;
    uint32_t unassignedUnitCount = 0;
    uint32_t activeLeaderCount = 0;
    uint32_t reserveLeaderCount = 0;
    uint64_t unitKey = 0;
    uint64_t activeLeaderKey = 0;
    uint64_t reserveLeaderKey = 0;
    std::uintptr_t unitAddress = 0;
    std::uintptr_t activeLeaderAddress = 0;
    std::uintptr_t reserveLeaderAddress = 0;
};

bool InspectLeaderPool(
    engine::EngineRegistry& engine,
    std::uintptr_t node,
    uint32_t& count,
    uint64_t& sampleKey,
    std::uintptr_t& sampleAddress,
    std::string& error
) noexcept
{
    count = 0;
    sampleKey = 0;
    sampleAddress = 0;
    for (std::size_t guard = 0;
         node && guard < MaximumLeaderNodes;
         ++guard)
    {
        std::uintptr_t leader = 0;
        std::uintptr_t next = 0;
        uint32_t id0 = 0;
        uint32_t id1 = 0;
        if (!TryRead(
                node + engine.FieldValue(
                    engine::FieldId::LeaderRegistryNodeLeaderOffset
                ),
                leader
            )
            || !leader
            || !TryRead(
                node + engine.FieldValue(
                    engine::FieldId::LeaderRegistryNodeNextOffset
                ),
                next
            )
            || !TryRead(
                leader + engine.FieldValue(
                    engine::FieldId::LeaderObjectId0Offset
                ),
                id0
            )
            || !TryRead(
                leader + engine.FieldValue(
                    engine::FieldId::LeaderObjectId1Offset
                ),
                id1
            ))
        {
            error = "hoi3_leader_registry_probe_unreadable";
            return false;
        }
        uint64_t stableId = 0;
        if (!PackHoi3LeaderKey(id0, id1, stableId))
        {
            error = "hoi3_leader_registry_probe_key_invalid";
            return false;
        }
        if (sampleKey == 0)
        {
            sampleKey = stableId;
            sampleAddress = leader;
        }
        ++count;
        if (next == node)
        {
            error = "hoi3_leader_registry_probe_cycle";
            return false;
        }
        node = next;
    }
    if (node)
    {
        error = "hoi3_leader_registry_probe_guard_exceeded";
        return false;
    }
    return true;
}

bool InspectUnitLeaderRegistries(
    engine::EngineRegistry& engine,
    std::string_view playerTag,
    UnitLeaderRegistryInspection& inspection,
    std::string& error
) noexcept
{
    uint64_t countryKey = 0;
    std::uintptr_t country = 0;
    engine::abi::CountryTagValue identity{};
    if (!PackHoi3CountryTag(playerTag, countryKey)
        || !ResolveCountryByStableTag(
            engine,
            countryKey,
            country,
            error
        )
        || !ReadCountryIdentity(engine, country, identity))
    {
        if (error.empty())
        {
            error = "hoi3_object_probe_country_unavailable";
        }
        return false;
    }
    inspection.countryIndex = identity.index;

    std::uintptr_t node = 0;
    std::uintptr_t tail = 0;
    uint32_t expectedCount = 0;
    if (!TryRead(
            country + engine.FieldValue(
                engine::FieldId::CountryUnitListHeadOffset
            ),
            node
        )
        || !TryRead(
            country + engine.FieldValue(
                engine::FieldId::CountryUnitListTailOffset
            ),
            tail
        )
        || !TryRead(
            country + engine.FieldValue(
                engine::FieldId::CountryUnitListCountOffset
            ),
            expectedCount
        )
        || expectedCount > MaximumUnitNodes
        || ((expectedCount == 0) != (node == 0))
        || ((expectedCount == 0) != (tail == 0)))
    {
        error = "hoi3_unit_registry_probe_header_invalid";
        return false;
    }

    std::uintptr_t previous = 0;
    uint32_t visited = 0;
    while (node && visited < expectedCount)
    {
        std::uintptr_t unit = 0;
        std::uintptr_t nodePrevious = 0;
        std::uintptr_t next = 0;
        std::uintptr_t leader = 0;
        uint32_t id0 = 0;
        uint32_t id1 = 0;
        uint32_t countryIndex = 0;
        if (!TryRead(
                node + engine.FieldValue(
                    engine::FieldId::UnitListNodeUnitOffset
                ),
                unit
            )
            || !unit
            || !TryRead(
                node + engine.FieldValue(
                    engine::FieldId::UnitListNodePreviousOffset
                ),
                nodePrevious
            )
            || !TryRead(
                node + engine.FieldValue(
                    engine::FieldId::UnitListNodeNextOffset
                ),
                next
            )
            || nodePrevious != previous
            || !TryRead(
                unit + engine.FieldValue(engine::FieldId::UnitId0Offset),
                id0
            )
            || !TryRead(
                unit + engine.FieldValue(engine::FieldId::UnitId1Offset),
                id1
            )
            || !TryRead(
                unit + engine.FieldValue(
                    engine::FieldId::UnitCountryIdOffset
                ),
                countryIndex
            )
            || countryIndex != identity.index
            || !TryRead(
                unit + engine.FieldValue(
                    engine::FieldId::UnitLeaderOffset
                ),
                leader
            ))
        {
            error = "hoi3_unit_registry_probe_node_invalid";
            return false;
        }
        uint64_t stableId = 0;
        if (!PackHoi3UnitKey(id0, id1, stableId))
        {
            error = "hoi3_unit_registry_probe_key_invalid";
            return false;
        }
        if (inspection.unitKey == 0)
        {
            inspection.unitKey = stableId;
            inspection.unitAddress = unit;
        }
        if (leader)
        {
            ++inspection.assignedUnitCount;
        }
        else
        {
            ++inspection.unassignedUnitCount;
        }
        ++visited;
        if (next == node)
        {
            error = "hoi3_unit_registry_probe_cycle";
            return false;
        }
        previous = node;
        node = next;
    }
    if (visited != expectedCount || node != 0 || previous != tail)
    {
        error = "hoi3_unit_registry_probe_invariant_failed";
        return false;
    }
    inspection.unitCount = visited;

    std::uintptr_t activeRoot = 0;
    std::uintptr_t reserveRoot = 0;
    if (!TryRead(
            country + engine.FieldValue(
                engine::FieldId::CountryLeaderRegistryOffset
            ),
            activeRoot
        )
        || !TryRead(
            country + engine.FieldValue(
                engine::FieldId::CountryReserveLeaderRegistryOffset
            ),
            reserveRoot
        )
        || !InspectLeaderPool(
            engine,
            activeRoot,
            inspection.activeLeaderCount,
            inspection.activeLeaderKey,
            inspection.activeLeaderAddress,
            error
        )
        || !InspectLeaderPool(
            engine,
            reserveRoot,
            inspection.reserveLeaderCount,
            inspection.reserveLeaderKey,
            inspection.reserveLeaderAddress,
            error
        ))
    {
        return false;
    }
    return true;
}

NativeQueryDescriptor QueryDescriptor(
    std::string operation,
    std::vector<engine::TypeId> types,
    std::vector<engine::FieldId> fields,
    std::vector<engine::SymbolId> symbols = {}
)
{
    NativeQueryDescriptor descriptor;
    descriptor.operation = std::move(operation);
    descriptor.provider = std::string(Provider);
    descriptor.requiredTypes = std::move(types);
    descriptor.requiredFields = std::move(fields);
    descriptor.requiredSymbols = std::move(symbols);
    return descriptor;
}

}

std::string_view Hoi3NativeQueriesModule::Id() const
{
    return "hoi3_native_queries";
}

int Hoi3NativeQueriesModule::Priority() const
{
    return 55;
}

bool Hoi3NativeQueriesModule::Initialize(
    Services& services,
    std::string& error
)
{
    diagnostic_ = services.diagnostic;
    engine_ = services.engine;
    queries_ = services.queries;
    resolvers_ = services.objectResolvers;
    reverseProbes_ = services.reverseProbes;
    if (!engine_ || !queries_ || !resolvers_)
    {
        error = "hoi3_native_query_services_missing";
        return false;
    }
    if (!engine_->IsActive())
    {
        if (diagnostic_)
        {
            diagnostic_("HOI3 native queries unavailable: engine inactive");
        }
        error.clear();
        return true;
    }
    if (!RegisterResolvers(error)
        || !RegisterQueries(error)
        || (reverseProbes_ && !RegisterReverseProbes(error)))
    {
        if (reverseProbes_)
        {
            reverseProbes_->Unregister(SameGenerationSnapshotProbeId);
            reverseProbes_->Unregister(UnitLeaderGenerationProbeId);
        }
        queries_->UnregisterProvider(Provider);
        resolvers_->UnregisterProvider(Provider);
        return false;
    }
    if (diagnostic_)
    {
        diagnostic_(
            "HOI3 native queries registered: queries="
            + std::to_string(queries_->Operations().size())
        );
    }
    error.clear();
    return true;
}

bool Hoi3NativeQueriesModule::RegisterResolvers(std::string& error)
{
    NativeObjectResolverDescriptor gameState;
    gameState.type = engine::TypeId::GameState;
    gameState.name = "game_state";
    gameState.provider = std::string(Provider);
    gameState.requiredSymbols = {engine::SymbolId::GameStateSingleton};
    if (!resolvers_->RegisterResolver(
            std::move(gameState),
            [](const NativeObjectKey& key,
               const NativeObjectResolveContext& context,
               std::uintptr_t& address,
               std::string& resolveError)
            {
                return key.stableId == SingletonStableId
                    && ResolveGameState(context.engine, address, resolveError);
            },
            error
        ))
    {
        return false;
    }

    NativeObjectResolverDescriptor countryDatabase;
    countryDatabase.type = engine::TypeId::CountryDatabase;
    countryDatabase.name = "country_database";
    countryDatabase.provider = std::string(Provider);
    countryDatabase.requiredSymbols = {
        engine::SymbolId::CountryDatabaseSingleton
    };
    if (!resolvers_->RegisterResolver(
            std::move(countryDatabase),
            [](const NativeObjectKey& key,
               const NativeObjectResolveContext& context,
               std::uintptr_t& address,
               std::string& resolveError)
            {
                return key.stableId == SingletonStableId
                    && ResolveCountryDatabase(
                        context.engine,
                        address,
                        resolveError
                    );
            },
            error
        ))
    {
        return false;
    }

    NativeObjectResolverDescriptor country;
    country.type = engine::TypeId::Country;
    country.name = "country_by_tag";
    country.provider = std::string(Provider);
    country.requiredSymbols = {
        engine::SymbolId::CountryDatabaseSingleton
    };
    country.requiredFields = {
        engine::FieldId::CountryPointerTableOffset,
        engine::FieldId::CountryTagOffset
    };
    if (!resolvers_->RegisterResolver(
            std::move(country),
            [](const NativeObjectKey& key,
               const NativeObjectResolveContext& context,
               std::uintptr_t& address,
               std::string& resolveError)
            {
                return ResolveCountryByStableTag(
                    context.engine,
                    key.stableId,
                    address,
                    resolveError
                );
            },
            error
        ))
    {
        return false;
    }

    NativeObjectResolverDescriptor province;
    province.type = engine::TypeId::Province;
    province.name = "province_by_id";
    province.provider = std::string(Provider);
    province.requiredSymbols = {engine::SymbolId::GameStateSingleton};
    province.requiredFields = {
        engine::FieldId::ProvinceVectorBeginOffset,
        engine::FieldId::ProvinceVectorEndOffset
    };
    if (!resolvers_->RegisterResolver(
            std::move(province),
            [](const NativeObjectKey& key,
               const NativeObjectResolveContext& context,
               std::uintptr_t& address,
               std::string& resolveError)
            {
                return ResolveProvinceById(
                    context.engine,
                    key.stableId,
                    address,
                    resolveError
                );
            },
            error
        ))
    {
        return false;
    }

    NativeObjectResolverDescriptor technologyDefinition;
    technologyDefinition.type = engine::TypeId::TechnologyDefinition;
    technologyDefinition.name = "technology_definition_by_name";
    technologyDefinition.provider = std::string(Provider);
    technologyDefinition.requiredSymbols = {
        engine::SymbolId::TechnologyLookup
    };
    if (!resolvers_->RegisterResolver(
            std::move(technologyDefinition),
            [](const NativeObjectKey& key,
               const NativeObjectResolveContext& context,
               std::uintptr_t& address,
               std::string& resolveError)
            {
                return key.stableId == 0
                    && ResolveTechnologyDefinitionByName(
                        context.engine,
                        key.stableName,
                        address,
                        resolveError
                    );
            },
            error
        ))
    {
        return false;
    }

    NativeObjectResolverDescriptor technologyStatus;
    technologyStatus.type = engine::TypeId::TechnologyStatus;
    technologyStatus.name = "technology_status_by_country";
    technologyStatus.provider = std::string(Provider);
    technologyStatus.requiredSymbols = {
        engine::SymbolId::CountryDatabaseSingleton
    };
    technologyStatus.requiredTypes = {engine::TypeId::Country};
    technologyStatus.requiredFields = {
        engine::FieldId::CountryPointerTableOffset,
        engine::FieldId::CountryTagOffset,
        engine::FieldId::CountryTechnologyModifierOffset
    };
    if (!resolvers_->RegisterResolver(
            std::move(technologyStatus),
            [](const NativeObjectKey& key,
               const NativeObjectResolveContext& context,
               std::uintptr_t& address,
               std::string& resolveError)
            {
                return ResolveTechnologyStatusByCountry(
                    context.engine,
                    key.stableId,
                    address,
                    resolveError
                );
            },
            error
        ))
    {
        return false;
    }

    NativeObjectResolverDescriptor relation;
    relation.type = engine::TypeId::Relation;
    relation.name = "relation_by_country_tags";
    relation.provider = std::string(Provider);
    relation.requiredSymbols = {
        engine::SymbolId::CountryDatabaseSingleton
    };
    relation.requiredTypes = {engine::TypeId::Country};
    relation.requiredFields = {
        engine::FieldId::CountryPointerTableOffset,
        engine::FieldId::CountryTagOffset,
        engine::FieldId::CountryRelationTableOffset
    };
    if (!resolvers_->RegisterResolver(
            std::move(relation),
            [](const NativeObjectKey& key,
               const NativeObjectResolveContext& context,
               std::uintptr_t& address,
               std::string& resolveError)
            {
                return ResolveRelationByStableTags(
                    context.engine,
                    key.stableId,
                    address,
                    resolveError
                );
            },
            error
        ))
    {
        return false;
    }

    NativeObjectResolverDescriptor unit;
    unit.type = engine::TypeId::Unit;
    unit.name = "unit_by_native_id";
    unit.provider = std::string(Provider);
    unit.requiredSymbols = {
        engine::SymbolId::CountryDatabaseSingleton
    };
    unit.requiredTypes = {
        engine::TypeId::Country,
        engine::TypeId::UnitListNode,
        engine::TypeId::Unit
    };
    unit.requiredFields = {
        engine::FieldId::CountryPointerTableOffset,
        engine::FieldId::CountryTagOffset,
        engine::FieldId::CountryUnitListHeadOffset,
        engine::FieldId::CountryUnitListTailOffset,
        engine::FieldId::CountryUnitListCountOffset,
        engine::FieldId::UnitListNodeUnitOffset,
        engine::FieldId::UnitListNodePreviousOffset,
        engine::FieldId::UnitListNodeNextOffset,
        engine::FieldId::UnitId0Offset,
        engine::FieldId::UnitId1Offset,
        engine::FieldId::UnitCountryIdOffset
    };
    if (!resolvers_->RegisterResolver(
            std::move(unit),
            [](const NativeObjectKey& key,
               const NativeObjectResolveContext& context,
               std::uintptr_t& address,
               std::string& resolveError)
            {
                return ResolveUnitByNativeId(
                    context.engine,
                    key.stableId,
                    key.stableName,
                    address,
                    resolveError
                );
            },
            error
        ))
    {
        return false;
    }

    NativeObjectResolverDescriptor leader;
    leader.type = engine::TypeId::Leader;
    leader.name = "leader_by_native_id";
    leader.provider = std::string(Provider);
    leader.requiredSymbols = {
        engine::SymbolId::CountryDatabaseSingleton
    };
    leader.requiredTypes = {
        engine::TypeId::Country,
        engine::TypeId::LeaderRegistryNode,
        engine::TypeId::Leader
    };
    leader.requiredFields = {
        engine::FieldId::CountryPointerTableOffset,
        engine::FieldId::CountryTagOffset,
        engine::FieldId::CountryLeaderRegistryOffset,
        engine::FieldId::CountryReserveLeaderRegistryOffset,
        engine::FieldId::LeaderRegistryNodeLeaderOffset,
        engine::FieldId::LeaderRegistryNodeNextOffset,
        engine::FieldId::LeaderObjectId0Offset,
        engine::FieldId::LeaderObjectId1Offset
    };
    return resolvers_->RegisterResolver(
        std::move(leader),
        [](const NativeObjectKey& key,
           const NativeObjectResolveContext& context,
           std::uintptr_t& address,
           std::string& resolveError)
        {
            return ResolveLeaderByNativeId(
                context.engine,
                key.stableId,
                key.stableName,
                address,
                resolveError
            );
        },
        error
    );
}

bool Hoi3NativeQueriesModule::RegisterQueries(std::string& error)
{
    const auto registerCountryFixed = [this, &error](
        const char* operation,
        engine::FieldId field
    )
    {
        return queries_->RegisterHandler(
            QueryDescriptor(
                operation,
                {engine::TypeId::Country},
                {field}
            ),
            [this, field](
                const NativeQueryRequest& request,
                const NativeQueryExecutionContext& context,
                NativeQueryValue& value,
                std::string& queryError
            )
            {
                uint64_t stableId = 0;
                engine::ObjectHandle country;
                int32_t raw = 0;
                if (!ReadRequestedTag(request, context, stableId, queryError)
                    || !ResolveGuarded(
                        *resolvers_,
                        context,
                        engine::TypeId::Country,
                        stableId,
                        country,
                        queryError
                    )
                    || !TryRead(
                        country.address + engine_->FieldValue(field),
                        raw
                    ))
                {
                    if (queryError.empty())
                    {
                        queryError = "hoi3_country_field_unavailable";
                    }
                    return false;
                }
                value = NativeQueryValue(
                    static_cast<double>(raw)
                    / engine::abi::FixedPointScale
                );
                return true;
            },
            error
        );
    };

    if (!registerCountryFixed(
            "country.manpower",
            engine::FieldId::CountryManpowerOffset
        )
        || !registerCountryFixed(
            "country.diplomatic_influence",
            engine::FieldId::CountryDiplomaticInfluenceOffset
        )
        || !registerCountryFixed(
            "country.total_leadership",
            engine::FieldId::CountryTotalLeadershipOffset
        )
        || !registerCountryFixed(
            "country.officer_pool",
            engine::FieldId::CountryOfficerPoolOffset
        )
        || !registerCountryFixed(
            "country.convoys",
            engine::FieldId::CountryConvoyPoolOffset
        )
        || !registerCountryFixed(
            "country.escorts",
            engine::FieldId::CountryEscortPoolOffset
        )
        || !registerCountryFixed(
            "country.free_spies",
            engine::FieldId::CountryFreeSpyPoolOffset
        )
        || !registerCountryFixed(
            "country.dissent",
            engine::FieldId::CountryDissentOffset
        )
        || !registerCountryFixed(
            "country.national_unity",
            engine::FieldId::CountryNationalUnityOffset
        )
        || !registerCountryFixed(
            "country.neutrality",
            engine::FieldId::CountryNeutralityOffset
        ))
    {
        return false;
    }

    if (!queries_->RegisterHandler(
            QueryDescriptor(
                "country.identity",
                {engine::TypeId::Country},
                {engine::FieldId::CountryTagOffset}
            ),
            [this](
                const NativeQueryRequest& request,
                const NativeQueryExecutionContext& context,
                NativeQueryValue& value,
                std::string& queryError
            )
            {
                uint64_t stableId = 0;
                engine::ObjectHandle country;
                engine::abi::CountryTagValue identity{};
                std::string tag;
                if (!ReadRequestedTag(request, context, stableId, queryError)
                    || !ResolveGuarded(
                        *resolvers_, context, engine::TypeId::Country,
                        stableId, country, queryError
                    )
                    || !ReadCountryIdentity(*engine_, country.address, identity)
                    || !UnpackHoi3CountryTag(identity.tag, tag))
                {
                    if (queryError.empty())
                    {
                        queryError = "hoi3_country_identity_unavailable";
                    }
                    return false;
                }
                value = NativeQueryValue::Object({
                    {"tag", NativeQueryValue(std::move(tag))},
                    {"country_index", NativeQueryValue(
                        static_cast<int64_t>(identity.index)
                    )},
                    {"generation", NativeQueryValue(
                        static_cast<int64_t>(country.lifecycleGeneration)
                    )}
                });
                return true;
            },
            error
        ))
    {
        return false;
    }

    if (!queries_->RegisterHandler(
            QueryDescriptor(
                "country.capital",
                {engine::TypeId::Country},
                {
                    engine::FieldId::CountryCapitalProvinceOffset,
                    engine::FieldId::CountryActingCapitalProvinceOffset
                }
            ),
            [this](
                const NativeQueryRequest& request,
                const NativeQueryExecutionContext& context,
                NativeQueryValue& value,
                std::string& queryError
            )
            {
                uint64_t stableId = 0;
                engine::ObjectHandle country;
                uint32_t official = 0;
                uint32_t acting = 0;
                if (!ReadRequestedTag(request, context, stableId, queryError)
                    || !ResolveGuarded(
                        *resolvers_, context, engine::TypeId::Country,
                        stableId, country, queryError
                    )
                    || !TryRead(
                        country.address + engine_->FieldValue(
                            engine::FieldId::CountryCapitalProvinceOffset
                        ), official
                    )
                    || !TryRead(
                        country.address + engine_->FieldValue(
                            engine::FieldId::CountryActingCapitalProvinceOffset
                        ), acting
                    ))
                {
                    if (queryError.empty())
                    {
                        queryError = "hoi3_country_capital_unavailable";
                    }
                    return false;
                }
                value = NativeQueryValue::Object({
                    {"official", NativeQueryValue(
                        static_cast<int64_t>(official)
                    )},
                    {"acting", NativeQueryValue(
                        static_cast<int64_t>(acting)
                    )}
                });
                return true;
            },
            error
        ))
    {
        return false;
    }

    if (!queries_->RegisterHandler(
            QueryDescriptor(
                "province.status",
                {engine::TypeId::Province},
                {
                    engine::FieldId::ProvinceOwnerOffset,
                    engine::FieldId::ProvinceControllerOffset,
                    engine::FieldId::ProvinceLeadershipOffset
                }
            ),
            [this](
                const NativeQueryRequest& request,
                const NativeQueryExecutionContext& context,
                NativeQueryValue& value,
                std::string& queryError
            )
            {
                uint64_t provinceId = 0;
                engine::ObjectHandle province;
                engine::abi::CountryTagValue owner{};
                engine::abi::CountryTagValue controller{};
                int32_t leadership = 0;
                std::string ownerTag;
                std::string controllerTag;
                if (!ReadProvinceId(request, provinceId, queryError)
                    || !ResolveGuarded(
                        *resolvers_, context, engine::TypeId::Province,
                        provinceId, province, queryError
                    )
                    || !TryRead(
                        province.address + engine_->FieldValue(
                            engine::FieldId::ProvinceOwnerOffset
                        ), owner
                    )
                    || !TryRead(
                        province.address + engine_->FieldValue(
                            engine::FieldId::ProvinceControllerOffset
                        ), controller
                    )
                    || !TryRead(
                        province.address + engine_->FieldValue(
                            engine::FieldId::ProvinceLeadershipOffset
                        ), leadership
                    )
                    || !CountryTagForIndex(*engine_, owner.index, ownerTag)
                    || !CountryTagForIndex(
                        *engine_, controller.index, controllerTag
                    ))
                {
                    if (queryError.empty())
                    {
                        queryError = "hoi3_province_status_unavailable";
                    }
                    return false;
                }
                value = NativeQueryValue::Object({
                    {"province_id", NativeQueryValue(
                        static_cast<int64_t>(provinceId)
                    )},
                    {"owner_tag", NativeQueryValue(std::move(ownerTag))},
                    {"owner_index", NativeQueryValue(
                        static_cast<int64_t>(owner.index)
                    )},
                    {"controller_tag", NativeQueryValue(
                        std::move(controllerTag)
                    )},
                    {"controller_index", NativeQueryValue(
                        static_cast<int64_t>(controller.index)
                    )},
                    {"leadership", NativeQueryValue(
                        static_cast<double>(leadership)
                        / engine::abi::FixedPointScale
                    )},
                    {"generation", NativeQueryValue(
                        static_cast<int64_t>(province.lifecycleGeneration)
                    )}
                });
                return true;
            },
            error
        ))
    {
        return false;
    }

    if (!queries_->RegisterHandler(
            QueryDescriptor(
                "technology.status",
                {
                    engine::TypeId::Country,
                    engine::TypeId::TechnologyDefinition,
                    engine::TypeId::TechnologyStatus,
                    engine::TypeId::ResearchNode
                },
                {
                    engine::FieldId::CountryResearchListHeadOffset,
                    engine::FieldId::CountryResearchListCountOffset,
                    engine::FieldId::TechnologyDefinitionIndexOffset,
                    engine::FieldId::TechnologyOneLevelOnlyOffset,
                    engine::FieldId::TechnologyMaximumLevelOffset,
                    engine::FieldId::TechnologyStatusLevelsOffset,
                    engine::FieldId::TechnologyStatusProgressOffset,
                    engine::FieldId::ResearchNodeTechnologyOffset,
                    engine::FieldId::ResearchNodeNextOffset,
                    engine::FieldId::ResearchCompletionThresholdValue
                },
                {engine::SymbolId::TechnologyLookup}
            ),
            [this](
                const NativeQueryRequest& request,
                const NativeQueryExecutionContext& context,
                NativeQueryValue& value,
                std::string& queryError
            )
            {
                uint64_t countryStableId = 0;
                std::string technologyName;
                engine::ObjectHandle country;
                engine::ObjectHandle definition;
                engine::ObjectHandle status;
                uint32_t technologyIndex = 0;
                uint8_t oneLevelOnly = 0;
                int32_t maximumLevel = 0;
                std::uintptr_t levels = 0;
                std::uintptr_t progresses = 0;
                int32_t level = 0;
                uint64_t progress = 0;
                bool researching = false;
                if (!ReadRequestedTag(
                        request,
                        context,
                        countryStableId,
                        queryError
                    )
                    || !ReadTechnologyName(
                        request,
                        technologyName,
                        queryError
                    )
                    || !ResolveGuarded(
                        *resolvers_, context, engine::TypeId::Country,
                        countryStableId, country, queryError
                    )
                    || !ResolveGuarded(
                        *resolvers_,
                        context,
                        engine::TypeId::TechnologyDefinition,
                        0,
                        definition,
                        queryError,
                        technologyName
                    )
                    || !ResolveGuarded(
                        *resolvers_,
                        context,
                        engine::TypeId::TechnologyStatus,
                        countryStableId,
                        status,
                        queryError
                    )
                    || !TryRead(
                        definition.address + engine_->FieldValue(
                            engine::FieldId::TechnologyDefinitionIndexOffset
                        ),
                        technologyIndex
                    )
                    || technologyIndex >= MaximumProvinceCount
                    || !TryRead(
                        definition.address + engine_->FieldValue(
                            engine::FieldId::TechnologyOneLevelOnlyOffset
                        ),
                        oneLevelOnly
                    )
                    || !TryRead(
                        definition.address + engine_->FieldValue(
                            engine::FieldId::TechnologyMaximumLevelOffset
                        ),
                        maximumLevel
                    )
                    || maximumLevel < 0
                    || !TryRead(
                        status.address + engine_->FieldValue(
                            engine::FieldId::TechnologyStatusLevelsOffset
                        ),
                        levels
                    )
                    || !levels
                    || !TryRead(
                        status.address + engine_->FieldValue(
                            engine::FieldId::TechnologyStatusProgressOffset
                        ),
                        progresses
                    )
                    || !progresses
                    || !TryRead(
                        levels + static_cast<std::uintptr_t>(technologyIndex)
                            * sizeof(int32_t),
                        level
                    )
                    || !TryRead(
                        progresses
                            + static_cast<std::uintptr_t>(technologyIndex)
                                * sizeof(uint64_t),
                        progress
                    )
                    || !IsTechnologyBeingResearched(
                        *engine_,
                        country.address,
                        definition.address,
                        researching,
                        queryError
                    ))
                {
                    if (queryError.empty())
                    {
                        queryError = "hoi3_technology_status_unavailable";
                    }
                    return false;
                }
                const std::uintptr_t completionThreshold =
                    engine_->FieldValue(
                        engine::FieldId::ResearchCompletionThresholdValue
                    );
                if (completionThreshold == 0)
                {
                    queryError = "hoi3_research_threshold_unavailable";
                    return false;
                }
                const double progressRatio = static_cast<double>(progress)
                    / static_cast<double>(completionThreshold);
                value = NativeQueryValue::Object({
                    {"technology", NativeQueryValue(technologyName)},
                    {"index", NativeQueryValue(
                        static_cast<int64_t>(technologyIndex)
                    )},
                    {"level", NativeQueryValue(
                        static_cast<int64_t>(level)
                    )},
                    {"maximum_level", NativeQueryValue(
                        static_cast<int64_t>(maximumLevel)
                    )},
                    {"one_level_only", NativeQueryValue(
                        oneLevelOnly != 0
                    )},
                    {"progress", NativeQueryValue(progressRatio)},
                    {"progress_raw", NativeQueryValue(
                        static_cast<int64_t>(progress)
                    )},
                    {"researching", NativeQueryValue(researching)},
                    {"generation", NativeQueryValue(
                        static_cast<int64_t>(status.lifecycleGeneration)
                    )}
                });
                return true;
            },
            error
        ))
    {
        return false;
    }

    if (!queries_->RegisterHandler(
            QueryDescriptor(
                "diplomacy.relation",
                {engine::TypeId::Relation},
                {
                    engine::FieldId::RelationValueOffset,
                    engine::FieldId::RelationThreatOffset
                }
            ),
            [this](
                const NativeQueryRequest& request,
                const NativeQueryExecutionContext& context,
                NativeQueryValue& value,
                std::string& queryError
            )
            {
                uint64_t sourceStableId = 0;
                uint64_t relationStableId = 0;
                std::string sourceTag;
                std::string targetTag;
                engine::ObjectHandle relation;
                int32_t relationValue = 0;
                int32_t threat = 0;
                if (!ReadRequestedTag(
                        request,
                        context,
                        sourceStableId,
                        queryError
                    )
                    || !UnpackHoi3CountryTag(sourceStableId, sourceTag)
                    || !ReadTargetTag(request, targetTag, queryError)
                    || !PackHoi3RelationKey(
                        sourceTag,
                        targetTag,
                        relationStableId
                    )
                    || !ResolveGuarded(
                        *resolvers_,
                        context,
                        engine::TypeId::Relation,
                        relationStableId,
                        relation,
                        queryError
                    )
                    || !TryRead(
                        relation.address + engine_->FieldValue(
                            engine::FieldId::RelationValueOffset
                        ),
                        relationValue
                    )
                    || !TryRead(
                        relation.address + engine_->FieldValue(
                            engine::FieldId::RelationThreatOffset
                        ),
                        threat
                    ))
                {
                    if (queryError.empty())
                    {
                        queryError = "hoi3_relation_status_unavailable";
                    }
                    return false;
                }
                value = NativeQueryValue::Object({
                    {"source_tag", NativeQueryValue(sourceTag)},
                    {"target_tag", NativeQueryValue(targetTag)},
                    {"value", NativeQueryValue(
                        static_cast<double>(relationValue)
                            / engine::abi::FixedPointScale
                    )},
                    {"threat", NativeQueryValue(
                        static_cast<double>(threat)
                            / engine::abi::FixedPointScale
                    )},
                    {"generation", NativeQueryValue(
                        static_cast<int64_t>(relation.lifecycleGeneration)
                    )}
                });
                return true;
            },
            error
        ))
    {
        return false;
    }

    if (!queries_->RegisterHandler(
            QueryDescriptor(
                "unit.status",
                {
                    engine::TypeId::Country,
                    engine::TypeId::Unit,
                    engine::TypeId::UnitListNode,
                    engine::TypeId::Leader
                },
                {
                    engine::FieldId::CountryPointerTableOffset,
                    engine::FieldId::CountryTagOffset,
                    engine::FieldId::CountryUnitListHeadOffset,
                    engine::FieldId::CountryUnitListTailOffset,
                    engine::FieldId::CountryUnitListCountOffset,
                    engine::FieldId::UnitListNodeUnitOffset,
                    engine::FieldId::UnitListNodePreviousOffset,
                    engine::FieldId::UnitListNodeNextOffset,
                    engine::FieldId::UnitId0Offset,
                    engine::FieldId::UnitId1Offset,
                    engine::FieldId::UnitCountryIdOffset,
                    engine::FieldId::UnitLeaderOffset,
                    engine::FieldId::LeaderObjectId0Offset,
                    engine::FieldId::LeaderObjectId1Offset
                }
            ),
            [this](
                const NativeQueryRequest& request,
                const NativeQueryExecutionContext& context,
                NativeQueryValue& value,
                std::string& queryError
            )
            {
                uint64_t stableId = 0;
                uint32_t id0 = 0;
                uint32_t id1 = 0;
                uint32_t countryIndex = 0;
                uint32_t leaderId0 = 0;
                uint32_t leaderId1 = 0;
                std::uintptr_t leaderAddress = 0;
                std::string ownerTag;
                std::string countryTag;
                engine::ObjectHandle unit;
                if (!ReadUnitKey(request, stableId, queryError)
                    || !ReadOwnerTag(
                        request,
                        context,
                        ownerTag,
                        queryError
                    )
                    || !ResolveGuarded(
                        *resolvers_,
                        context,
                        engine::TypeId::Unit,
                        stableId,
                        unit,
                        queryError,
                        ownerTag
                    )
                    || !UnpackHoi3UnitKey(stableId, id0, id1)
                    || !TryRead(
                        unit.address + engine_->FieldValue(
                            engine::FieldId::UnitCountryIdOffset
                        ),
                        countryIndex
                    )
                    || !TryRead(
                        unit.address + engine_->FieldValue(
                            engine::FieldId::UnitLeaderOffset
                        ),
                        leaderAddress
                    )
                    || (leaderAddress
                        && (!TryRead(
                                leaderAddress + engine_->FieldValue(
                                    engine::FieldId::LeaderObjectId0Offset
                                ),
                                leaderId0
                            )
                            || !TryRead(
                                leaderAddress + engine_->FieldValue(
                                    engine::FieldId::LeaderObjectId1Offset
                                ),
                                leaderId1
                            )))
                    || !CountryTagForIndex(
                        *engine_,
                        countryIndex,
                        countryTag
                    ))
                {
                    if (queryError.empty())
                    {
                        queryError = "hoi3_unit_status_unavailable";
                    }
                    return false;
                }
                value = NativeQueryValue::Object({
                    {"unit_id0", NativeQueryValue(
                        static_cast<int64_t>(id0)
                    )},
                    {"unit_id1", NativeQueryValue(
                        static_cast<int64_t>(id1)
                    )},
                    {"country_tag", NativeQueryValue(countryTag)},
                    {"country_index", NativeQueryValue(
                        static_cast<int64_t>(countryIndex)
                    )},
                    {"has_leader", NativeQueryValue(
                        leaderAddress != 0
                    )},
                    {"leader_id0", NativeQueryValue(
                        static_cast<int64_t>(leaderId0)
                    )},
                    {"leader_id1", NativeQueryValue(
                        static_cast<int64_t>(leaderId1)
                    )},
                    {"resolver_scope", NativeQueryValue(
                        "country_unit_registry"
                    )},
                    {"generation", NativeQueryValue(
                        static_cast<int64_t>(unit.lifecycleGeneration)
                    )}
                });
                return true;
            },
            error
        ))
    {
        return false;
    }

    if (!queries_->RegisterHandler(
            QueryDescriptor(
                "leader.status",
                {
                    engine::TypeId::Country,
                    engine::TypeId::Leader,
                    engine::TypeId::LeaderRegistryNode,
                    engine::TypeId::Unit
                },
                {
                    engine::FieldId::CountryPointerTableOffset,
                    engine::FieldId::CountryTagOffset,
                    engine::FieldId::CountryLeaderRegistryOffset,
                    engine::FieldId::CountryReserveLeaderRegistryOffset,
                    engine::FieldId::LeaderRegistryNodeLeaderOffset,
                    engine::FieldId::LeaderRegistryNodeNextOffset,
                    engine::FieldId::LeaderObjectId0Offset,
                    engine::FieldId::LeaderObjectId1Offset,
                    engine::FieldId::LeaderUnitReverseOffset,
                    engine::FieldId::UnitId0Offset,
                    engine::FieldId::UnitId1Offset,
                    engine::FieldId::UnitCountryIdOffset
                }
            ),
            [this](
                const NativeQueryRequest& request,
                const NativeQueryExecutionContext& context,
                NativeQueryValue& value,
                std::string& queryError
            )
            {
                uint64_t stableId = 0;
                uint64_t ownerStableId = 0;
                uint32_t leaderId0 = 0;
                uint32_t leaderId1 = 0;
                uint32_t resolvedLeaderId0 = 0;
                uint32_t resolvedLeaderId1 = 0;
                uint32_t unitId0 = 0;
                uint32_t unitId1 = 0;
                uint32_t countryIndex = 0;
                std::uintptr_t unitAddress = 0;
                std::string ownerTag;
                engine::ObjectHandle leader;
                engine::ObjectHandle country;
                engine::abi::CountryTagValue identity{};
                if (!ReadLeaderKey(request, stableId, queryError)
                    || !ReadOwnerTag(
                        request,
                        context,
                        ownerTag,
                        queryError
                    )
                    || !PackHoi3CountryTag(ownerTag, ownerStableId)
                    || !ResolveGuarded(
                        *resolvers_,
                        context,
                        engine::TypeId::Country,
                        ownerStableId,
                        country,
                        queryError
                    )
                    || !ResolveGuarded(
                        *resolvers_,
                        context,
                        engine::TypeId::Leader,
                        stableId,
                        leader,
                        queryError,
                        ownerTag
                    )
                    || !ReadCountryIdentity(
                        *engine_,
                        country.address,
                        identity
                    )
                    || !UnpackHoi3LeaderKey(
                        stableId,
                        leaderId0,
                        leaderId1
                    )
                    || !TryRead(
                        leader.address + engine_->FieldValue(
                            engine::FieldId::LeaderObjectId0Offset
                        ),
                        resolvedLeaderId0
                    )
                    || !TryRead(
                        leader.address + engine_->FieldValue(
                            engine::FieldId::LeaderObjectId1Offset
                        ),
                        resolvedLeaderId1
                    )
                    || resolvedLeaderId0 != leaderId0
                    || resolvedLeaderId1 != leaderId1
                    || !TryRead(
                        leader.address + engine_->FieldValue(
                            engine::FieldId::LeaderUnitReverseOffset
                        ),
                        unitAddress
                    )
                    || (unitAddress
                        && (!TryRead(
                                unitAddress + engine_->FieldValue(
                                    engine::FieldId::UnitId0Offset
                                ),
                                unitId0
                            )
                            || !TryRead(
                                unitAddress + engine_->FieldValue(
                                    engine::FieldId::UnitId1Offset
                                ),
                                unitId1
                            )
                            || !TryRead(
                                unitAddress + engine_->FieldValue(
                                    engine::FieldId::UnitCountryIdOffset
                                ),
                                countryIndex
                            ))))
                {
                    if (queryError.empty())
                    {
                        queryError = "hoi3_leader_status_unavailable";
                    }
                    return false;
                }
                if (!unitAddress)
                {
                    countryIndex = identity.index;
                }
                value = NativeQueryValue::Object({
                    {"leader_id0", NativeQueryValue(
                        static_cast<int64_t>(leaderId0)
                    )},
                    {"leader_id1", NativeQueryValue(
                        static_cast<int64_t>(leaderId1)
                    )},
                    {"country_tag", NativeQueryValue(ownerTag)},
                    {"country_index", NativeQueryValue(
                        static_cast<int64_t>(countryIndex)
                    )},
                    {"has_unit", NativeQueryValue(unitAddress != 0)},
                    {"unit_id0", NativeQueryValue(
                        static_cast<int64_t>(unitId0)
                    )},
                    {"unit_id1", NativeQueryValue(
                        static_cast<int64_t>(unitId1)
                    )},
                    {"resolver_scope", NativeQueryValue(
                        "country_leader_registry"
                    )},
                    {"generation", NativeQueryValue(
                        static_cast<int64_t>(leader.lifecycleGeneration)
                    )}
                });
                return true;
            },
            error
        ))
    {
        return false;
    }

    return queries_->RegisterHandler(
        QueryDescriptor(
            "game.current_date.total_days",
            {engine::TypeId::GameState},
            {engine::FieldId::CurrentDateOffset},
            {
                engine::SymbolId::GameStateSingleton,
                engine::SymbolId::CurrentDateTotalDays
            }
        ),
        [this](
            const NativeQueryRequest&,
            const NativeQueryExecutionContext& context,
            NativeQueryValue& value,
            std::string& queryError
        )
        {
            engine::ObjectHandle gameState;
            if (!ResolveGuarded(
                    *resolvers_, context, engine::TypeId::GameState,
                    SingletonStableId, gameState, queryError
                ))
            {
                return false;
            }
#if defined(_MSC_VER) && defined(_M_IX86)
            const std::uintptr_t function = engine_->Resolve(
                engine::SymbolId::CurrentDateTotalDays
            );
            const std::uintptr_t date = gameState.address
                + engine_->FieldValue(engine::FieldId::CurrentDateOffset);
            int32_t totalDays = 0;
            if (TryReadCurrentDateTotalDays(function, date, totalDays))
            {
                value = NativeQueryValue(static_cast<int64_t>(totalDays));
                return true;
            }
            queryError = "hoi3_current_date_unavailable";
            return false;
#else
            (void)value;
            queryError = "hoi3_native_query_requires_x86";
            return false;
#endif
        },
        error
    );
}

bool Hoi3NativeQueriesModule::RegisterReverseProbes(std::string& error)
{
    ReverseProbeDefinition snapshot;
    snapshot.id = std::string(SameGenerationSnapshotProbeId);
    snapshot.category = "hoi3_native_query";
    snapshot.access = ReverseProbeAccess::ReadMemory;
    snapshot.requiredVersion = engine::VersionId::Hoi3Tfh402D328;
    snapshot.requiredCapabilities = {
        "query.country.identity",
        "query.country.manpower",
        "query.country.total_leadership",
        "query.country.capital",
        "query.game.current_date.total_days",
        "query.diplomacy.relation"
    };
    snapshot.requiresGameplay = true;
    snapshot.requiresStableBarrier = true;
    snapshot.execute = [this](const ReverseProbeContext& context)
    {
        return ProbeSameGenerationSnapshot(context);
    };
    if (!reverseProbes_->Register(std::move(snapshot), error))
    {
        return false;
    }

    ReverseProbeDefinition objects;
    objects.id = std::string(UnitLeaderGenerationProbeId);
    objects.category = "hoi3_object_registry";
    objects.access = ReverseProbeAccess::ReadMemory;
    objects.requiredVersion = engine::VersionId::Hoi3Tfh402D328;
    objects.requiredSymbols = {
        engine::SymbolId::CountryDatabaseSingleton
    };
    objects.requiredTypes = {
        engine::TypeId::Country,
        engine::TypeId::Unit,
        engine::TypeId::UnitListNode,
        engine::TypeId::Leader,
        engine::TypeId::LeaderRegistryNode
    };
    objects.requiredFields = {
        engine::FieldId::CountryPointerTableOffset,
        engine::FieldId::CountryTagOffset,
        engine::FieldId::CountryUnitListHeadOffset,
        engine::FieldId::CountryUnitListTailOffset,
        engine::FieldId::CountryUnitListCountOffset,
        engine::FieldId::CountryLeaderRegistryOffset,
        engine::FieldId::CountryReserveLeaderRegistryOffset,
        engine::FieldId::UnitListNodeUnitOffset,
        engine::FieldId::UnitListNodePreviousOffset,
        engine::FieldId::UnitListNodeNextOffset,
        engine::FieldId::UnitId0Offset,
        engine::FieldId::UnitId1Offset,
        engine::FieldId::UnitCountryIdOffset,
        engine::FieldId::UnitLeaderOffset,
        engine::FieldId::LeaderRegistryNodeLeaderOffset,
        engine::FieldId::LeaderRegistryNodeNextOffset,
        engine::FieldId::LeaderObjectId0Offset,
        engine::FieldId::LeaderObjectId1Offset,
        engine::FieldId::LeaderUnitReverseOffset
    };
    objects.requiredCapabilities = {
        "resolver.unit_by_native_id",
        "resolver.leader_by_native_id",
        "query.unit.status",
        "query.leader.status"
    };
    objects.requiresGameplay = true;
    objects.requiresStableBarrier = true;
    objects.execute = [this](const ReverseProbeContext& context)
    {
        return ProbeUnitLeaderGeneration(context);
    };
    if (!reverseProbes_->Register(std::move(objects), error))
    {
        reverseProbes_->Unregister(SameGenerationSnapshotProbeId);
        return false;
    }
    error.clear();
    return true;
}

ReverseProbeResult Hoi3NativeQueriesModule::ProbeSameGenerationSnapshot(
    const ReverseProbeContext& context
)
{
    ReverseProbeResult result;
    if (!context.queries
        || !context.safetyLease
        || context.lifecycle.playerTag.empty()
        || context.callerThreadId == 0)
    {
        result.status = ReverseProbeStatus::Skipped;
        result.message = "hoi3_query_snapshot_context_unavailable";
        return result;
    }

    const std::string& playerTag = context.lifecycle.playerTag;
    const std::string relationTarget = playerTag == "JAP" ? "CHI" : "JAP";
    const auto countryRequest = [&playerTag](
        std::string key,
        std::string operation
    )
    {
        NativeQueryRequest request;
        request.key = std::move(key);
        request.operation = std::move(operation);
        request.arguments.emplace("tag", NativeQueryValue(playerTag));
        return request;
    };

    std::vector<NativeQueryRequest> requests;
    requests.push_back(countryRequest("identity", "country.identity"));
    requests.push_back(countryRequest("manpower", "country.manpower"));
    requests.push_back(countryRequest(
        "leadership",
        "country.total_leadership"
    ));
    requests.push_back(countryRequest("capital", "country.capital"));
    NativeQueryRequest date;
    date.key = "date";
    date.operation = "game.current_date.total_days";
    requests.push_back(std::move(date));
    NativeQueryRequest relation = countryRequest(
        "relation",
        "diplomacy.relation"
    );
    relation.arguments.emplace(
        "target_tag",
        NativeQueryValue(relationTarget)
    );
    requests.push_back(std::move(relation));

    NativeQuerySnapshot snapshot = context.queries->ExecuteSnapshotGuarded(
        std::move(requests),
        context.callerStateId,
        context.callerThreadId,
        context.safetyLease
    );
    if (!snapshot.Succeeded() || snapshot.results.size() != 6)
    {
        result.status = ReverseProbeStatus::Failed;
        result.evidence = ReverseProbeEvidence::Confirmed;
        result.message = "hoi3_query_snapshot_failed: "
            + snapshot.code + ":" + snapshot.message;
        return result;
    }
    result.status = ReverseProbeStatus::Passed;
    result.evidence = ReverseProbeEvidence::Proven;
    result.message = "snapshot_id="
        + std::to_string(snapshot.snapshotId)
        + ",save_generation="
        + std::to_string(context.lifecycle.saveGeneration)
        + ",generation="
        + std::to_string(snapshot.lifecycleGeneration)
        + ",player=" + snapshot.playerTag
        + ",queries=" + std::to_string(snapshot.results.size());
    return result;
}

ReverseProbeResult Hoi3NativeQueriesModule::ProbeUnitLeaderGeneration(
    const ReverseProbeContext& context
)
{
    ReverseProbeResult result;
    if (!context.engine
        || !context.queries
        || !context.objectResolvers
        || !context.safetyLease
        || context.lifecycle.playerTag.empty()
        || context.callerThreadId == 0)
    {
        result.status = ReverseProbeStatus::Skipped;
        result.message = "hoi3_object_probe_context_unavailable";
        return result;
    }

    UnitLeaderRegistryInspection inspection;
    std::string error;
    if (!InspectUnitLeaderRegistries(
            *context.engine,
            context.lifecycle.playerTag,
            inspection,
            error
        ))
    {
        result.status = ReverseProbeStatus::Failed;
        result.evidence = ReverseProbeEvidence::Confirmed;
        result.message = error;
        return result;
    }

    std::vector<NativeQueryRequest> requests;
    const auto appendObjectRequest = [&requests, &context](
        std::string key,
        std::string operation,
        uint64_t stableKey,
        bool leader
    )
    {
        if (stableKey == 0)
        {
            return;
        }
        uint32_t id0 = 0;
        uint32_t id1 = 0;
        const bool unpacked = leader
            ? UnpackHoi3LeaderKey(stableKey, id0, id1)
            : UnpackHoi3UnitKey(stableKey, id0, id1);
        if (!unpacked)
        {
            return;
        }
        NativeQueryRequest request;
        request.key = std::move(key);
        request.operation = std::move(operation);
        request.arguments.emplace(
            leader ? "leader_id0" : "unit_id0",
            NativeQueryValue(static_cast<int64_t>(id0))
        );
        request.arguments.emplace(
            leader ? "leader_id1" : "unit_id1",
            NativeQueryValue(static_cast<int64_t>(id1))
        );
        request.arguments.emplace(
            "tag",
            NativeQueryValue(context.lifecycle.playerTag)
        );
        requests.push_back(std::move(request));
    };
    appendObjectRequest(
        "unit",
        "unit.status",
        inspection.unitKey,
        false
    );
    appendObjectRequest(
        "active_leader",
        "leader.status",
        inspection.activeLeaderKey,
        true
    );
    appendObjectRequest(
        "reserve_leader",
        "leader.status",
        inspection.reserveLeaderKey,
        true
    );

    uint64_t snapshotId = 0;
    if (!requests.empty())
    {
        NativeQuerySnapshot snapshot =
            context.queries->ExecuteSnapshotGuarded(
                std::move(requests),
                context.callerStateId,
                context.callerThreadId,
                context.safetyLease
            );
        if (!snapshot.Succeeded())
        {
            result.status = ReverseProbeStatus::Failed;
            result.evidence = ReverseProbeEvidence::Confirmed;
            result.message = "hoi3_object_query_snapshot_failed: "
                + snapshot.code + ":" + snapshot.message;
            return result;
        }
        snapshotId = snapshot.snapshotId;
    }

    std::size_t previousResolved = 0;
    std::size_t previousChangedAddress = 0;
    std::size_t previousMissing = 0;
    bool comparedGeneration = false;
    {
        std::lock_guard<std::mutex> lock(objectProbeMutex_);
        ObjectProbeBaseline& baseline = objectProbeBaseline_;
        comparedGeneration = baseline.valid
            && baseline.playerTag == context.lifecycle.playerTag
            && context.lifecycle.saveGeneration > baseline.saveGeneration;
        if (comparedGeneration)
        {
            const auto resolvePrevious = [
                &context,
                &previousResolved,
                &previousChangedAddress,
                &previousMissing
            ](
                engine::TypeId type,
                uint64_t stableKey,
                std::uintptr_t previousAddress
            )
            {
                if (stableKey == 0)
                {
                    return;
                }
                engine::ObjectHandle handle;
                std::string resolveError;
                if (!context.objectResolvers->ResolveGuarded(
                        {
                            type,
                            stableKey,
                            context.lifecycle.playerTag
                        },
                        context.safetyLease,
                        handle,
                        resolveError
                    ))
                {
                    ++previousMissing;
                    return;
                }
                ++previousResolved;
                if (handle.address != previousAddress)
                {
                    ++previousChangedAddress;
                }
            };
            resolvePrevious(
                engine::TypeId::Unit,
                baseline.unitKey,
                baseline.unitAddress
            );
            resolvePrevious(
                engine::TypeId::Leader,
                baseline.activeLeaderKey,
                baseline.activeLeaderAddress
            );
            resolvePrevious(
                engine::TypeId::Leader,
                baseline.reserveLeaderKey,
                baseline.reserveLeaderAddress
            );
        }
        baseline.valid = true;
        baseline.playerTag = context.lifecycle.playerTag;
        baseline.saveGeneration = context.lifecycle.saveGeneration;
        baseline.unitKey = inspection.unitKey;
        baseline.activeLeaderKey = inspection.activeLeaderKey;
        baseline.reserveLeaderKey = inspection.reserveLeaderKey;
        baseline.unitAddress = inspection.unitAddress;
        baseline.activeLeaderAddress = inspection.activeLeaderAddress;
        baseline.reserveLeaderAddress = inspection.reserveLeaderAddress;
    }

    result.status = ReverseProbeStatus::Passed;
    result.evidence = comparedGeneration && previousResolved > 0
        ? ReverseProbeEvidence::Proven
        : ReverseProbeEvidence::Confirmed;
    std::ostringstream message;
    message
        << "country_index=" << inspection.countryIndex
        << ",units=" << inspection.unitCount
        << ",assigned_units=" << inspection.assignedUnitCount
        << ",unassigned_units=" << inspection.unassignedUnitCount
        << ",active_leaders=" << inspection.activeLeaderCount
        << ",reserve_leaders=" << inspection.reserveLeaderCount
        << ",snapshot_id=" << snapshotId
        << ",save_generation="
        << context.lifecycle.saveGeneration;
    if (comparedGeneration)
    {
        message
            << ",previous_resolved=" << previousResolved
            << ",previous_address_changed=" << previousChangedAddress
            << ",previous_missing=" << previousMissing;
    }
    else
    {
        message << ",generation_baseline=captured";
    }
    result.message = message.str();
    return result;
}

void Hoi3NativeQueriesModule::OnLifecycleEvent(const LifecycleEvent&)
{
}

void Hoi3NativeQueriesModule::Tick(uint64_t)
{
}

void Hoi3NativeQueriesModule::Shutdown()
{
    if (reverseProbes_)
    {
        reverseProbes_->Unregister(SameGenerationSnapshotProbeId);
        reverseProbes_->Unregister(UnitLeaderGenerationProbeId);
    }
    if (queries_)
    {
        queries_->UnregisterProvider(Provider);
    }
    if (resolvers_)
    {
        resolvers_->UnregisterProvider(Provider);
    }
    {
        std::lock_guard<std::mutex> lock(objectProbeMutex_);
        objectProbeBaseline_ = {};
    }
    diagnostic_ = {};
    engine_ = nullptr;
    queries_ = nullptr;
    resolvers_ = nullptr;
    reverseProbes_ = nullptr;
}

}
