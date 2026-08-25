#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>

#include "capability_registry.h"
#include "core_hook_registry.h"
#include "core_lifecycle.h"
#include "engine_abi_hoi3_tfh_402.h"
#include "engine_registry.h"
#include "hoi3_native_object_keys.h"
#include "hoi3_native_queries_module.h"
#include "native_object_resolver.h"
#include "native_query_service.h"
#include "native_save_load_barrier.h"
#include "reverse_probe_framework.h"

namespace
{

constexpr std::size_t ProbeArenaSize = 0x02000000;
constexpr uint32_t CountryIndex = 7;
constexpr uint32_t AssignedUnitId0 = 10;
constexpr uint32_t AssignedUnitId1 = 20;
constexpr uint32_t UnassignedUnitId0 = 30;
constexpr uint32_t UnassignedUnitId1 = 40;
constexpr uint32_t ActiveLeaderId0 = 0x1268;
constexpr uint32_t ActiveLeaderId1 = 101;
constexpr uint32_t ReserveLeaderId0 = 0x1268;
constexpr uint32_t ReserveLeaderId1 = 102;

template <typename T>
void WriteValue(std::uintptr_t address, const T& value)
{
    std::memcpy(reinterpret_cast<void*>(address), &value, sizeof(T));
}

void WritePointer(std::uintptr_t address, std::uintptr_t value)
{
    static_assert(sizeof(std::uintptr_t) == sizeof(uint32_t));
    WriteValue(address, static_cast<uint32_t>(value));
}

bool ReadIntegerField(
    const core::NativeQueryValue& value,
    const char* name,
    int64_t& output
)
{
    const core::NativeQueryValue* field = value.Find(name);
    return field && core::NativeQueryValueToInteger(*field, output);
}

bool ReadBooleanField(
    const core::NativeQueryValue& value,
    const char* name,
    bool& output
)
{
    const core::NativeQueryValue* field = value.Find(name);
    return field && core::NativeQueryValueToBool(*field, output);
}

}

int main()
{
    void* allocation = VirtualAlloc(
        nullptr,
        ProbeArenaSize,
        MEM_COMMIT | MEM_RESERVE,
        PAGE_READWRITE
    );
    if (!allocation)
    {
        std::cerr << "Probe arena allocation failed\n";
        return 1;
    }
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(
        allocation
    );

    core::engine::EngineRegistry engine;
    const auto& profile = core::engine::Hoi3Tfh402D328Profile();
    std::string error;
    if (!engine.SelectVersion(profile.version.executable, base, error))
    {
        std::cerr << "Engine setup failed: " << error << '\n';
        VirtualFree(allocation, 0, MEM_RELEASE);
        return 2;
    }
    engine.ObserveLifecycleGeneration(1);

    const std::uintptr_t database = base + 0x1000;
    const std::uintptr_t countryTable = base + 0x3000;
    const std::uintptr_t country = base + 0x8000;
    const std::uintptr_t firstUnitNode = base + 0xA000;
    const std::uintptr_t secondUnitNode = base + 0xA100;
    const std::uintptr_t assignedUnit = base + 0xB000;
    const std::uintptr_t unassignedUnit = base + 0xB400;
    const std::uintptr_t activeLeaderNode = base + 0xC000;
    const std::uintptr_t activeLeader = base + 0xC100;
    const std::uintptr_t reserveLeaderNode = base + 0xD000;
    const std::uintptr_t reserveLeader = base + 0xD100;

    WritePointer(
        engine.Resolve(
            core::engine::SymbolId::CountryDatabaseSingleton
        ),
        database
    );
    WritePointer(
        database + engine.FieldValue(
            core::engine::FieldId::CountryPointerTableOffset
        ),
        countryTable
    );
    WritePointer(
        countryTable + CountryIndex * sizeof(uint32_t),
        country
    );

    uint64_t countryStableId = 0;
    if (!core::PackHoi3CountryTag("CHI", countryStableId))
    {
        std::cerr << "Country tag packing failed\n";
        VirtualFree(allocation, 0, MEM_RELEASE);
        return 3;
    }
    const core::engine::abi::CountryTagValue countryIdentity{
        static_cast<uint32_t>(countryStableId & 0x00FFFFFFu),
        CountryIndex
    };
    WriteValue(
        country + engine.FieldValue(
            core::engine::FieldId::CountryTagOffset
        ),
        countryIdentity
    );

    WritePointer(
        country + engine.FieldValue(
            core::engine::FieldId::CountryUnitListHeadOffset
        ),
        firstUnitNode
    );
    WritePointer(
        country + engine.FieldValue(
            core::engine::FieldId::CountryUnitListTailOffset
        ),
        secondUnitNode
    );
    WriteValue(
        country + engine.FieldValue(
            core::engine::FieldId::CountryUnitListCountOffset
        ),
        uint32_t{2}
    );
    WritePointer(
        firstUnitNode + engine.FieldValue(
            core::engine::FieldId::UnitListNodeUnitOffset
        ),
        assignedUnit
    );
    WritePointer(
        firstUnitNode + engine.FieldValue(
            core::engine::FieldId::UnitListNodeNextOffset
        ),
        secondUnitNode
    );
    WritePointer(
        secondUnitNode + engine.FieldValue(
            core::engine::FieldId::UnitListNodeUnitOffset
        ),
        unassignedUnit
    );
    WritePointer(
        secondUnitNode + engine.FieldValue(
            core::engine::FieldId::UnitListNodePreviousOffset
        ),
        firstUnitNode
    );

    WriteValue(
        assignedUnit + engine.FieldValue(
            core::engine::FieldId::UnitId0Offset
        ),
        AssignedUnitId0
    );
    WriteValue(
        assignedUnit + engine.FieldValue(
            core::engine::FieldId::UnitId1Offset
        ),
        AssignedUnitId1
    );
    WriteValue(
        assignedUnit + engine.FieldValue(
            core::engine::FieldId::UnitCountryIdOffset
        ),
        CountryIndex
    );
    WritePointer(
        assignedUnit + engine.FieldValue(
            core::engine::FieldId::UnitLeaderOffset
        ),
        activeLeader
    );
    WriteValue(
        unassignedUnit + engine.FieldValue(
            core::engine::FieldId::UnitId0Offset
        ),
        UnassignedUnitId0
    );
    WriteValue(
        unassignedUnit + engine.FieldValue(
            core::engine::FieldId::UnitId1Offset
        ),
        UnassignedUnitId1
    );
    WriteValue(
        unassignedUnit + engine.FieldValue(
            core::engine::FieldId::UnitCountryIdOffset
        ),
        CountryIndex
    );

    WritePointer(
        country + engine.FieldValue(
            core::engine::FieldId::CountryLeaderRegistryOffset
        ),
        activeLeaderNode
    );
    WritePointer(
        country + engine.FieldValue(
            core::engine::FieldId::CountryReserveLeaderRegistryOffset
        ),
        reserveLeaderNode
    );
    WritePointer(
        activeLeaderNode + engine.FieldValue(
            core::engine::FieldId::LeaderRegistryNodeLeaderOffset
        ),
        activeLeader
    );
    WritePointer(
        reserveLeaderNode + engine.FieldValue(
            core::engine::FieldId::LeaderRegistryNodeLeaderOffset
        ),
        reserveLeader
    );
    WriteValue(
        activeLeader + engine.FieldValue(
            core::engine::FieldId::LeaderObjectId0Offset
        ),
        ActiveLeaderId0
    );
    WriteValue(
        activeLeader + engine.FieldValue(
            core::engine::FieldId::LeaderObjectId1Offset
        ),
        ActiveLeaderId1
    );
    WritePointer(
        activeLeader + engine.FieldValue(
            core::engine::FieldId::LeaderUnitReverseOffset
        ),
        assignedUnit
    );
    WriteValue(
        reserveLeader + engine.FieldValue(
            core::engine::FieldId::LeaderObjectId0Offset
        ),
        ReserveLeaderId0
    );
    WriteValue(
        reserveLeader + engine.FieldValue(
            core::engine::FieldId::LeaderObjectId1Offset
        ),
        ReserveLeaderId1
    );

    core::CapabilityRegistry capabilities;
    core::NativeObjectResolverService resolvers;
    core::NativeQueryService queries;
    core::NativeSaveLoadBarrier saveLoadBarrier(3);
    core::ReverseProbeFramework reverseProbes;
    resolvers.Configure(&engine, &capabilities);
    queries.Configure(&capabilities, &engine);
    queries.SetSafetyGate([]() -> std::shared_ptr<void>
    {
        return std::static_pointer_cast<void>(std::make_shared<int>(1));
    });

    core::HookRegistry hooks;
    core::LifecycleService lifecycle;
    core::Services services{hooks, lifecycle, {}};
    services.engine = &engine;
    services.queries = &queries;
    services.objectResolvers = &resolvers;
    services.capabilities = &capabilities;
    services.saveLoadBarrier = &saveLoadBarrier;
    services.reverseProbes = &reverseProbes;

    core::Hoi3NativeQueriesModule module;
    if (!module.Initialize(services, error))
    {
        std::cerr << "Module initialization failed: " << error << '\n';
        VirtualFree(allocation, 0, MEM_RELEASE);
        return 4;
    }

    if (!resolvers.HasResolver(core::engine::TypeId::TechnologyDefinition)
        || !resolvers.HasResolver(core::engine::TypeId::TechnologyStatus)
        || !resolvers.HasResolver(core::engine::TypeId::Relation)
        || !resolvers.HasResolver(core::engine::TypeId::Unit)
        || !resolvers.HasResolver(core::engine::TypeId::Leader))
    {
        std::cerr << "HOI3 resolver registration incomplete\n";
        return 5;
    }

    if (!queries.HasHandler("technology.status")
        || !queries.HasHandler("diplomacy.relation")
        || !queries.HasHandler("unit.status")
        || !queries.HasHandler("leader.status"))
    {
        std::cerr << "HOI3 query registration incomplete\n";
        return 6;
    }

    if (!capabilities.Contains("resolver.technology_definition_by_name")
        || !capabilities.Contains("resolver.technology_status_by_country")
        || !capabilities.Contains("resolver.relation_by_country_tags")
        || !capabilities.Contains("resolver.unit_by_native_id")
        || !capabilities.Contains("resolver.leader_by_native_id")
        || !capabilities.Contains("query.technology.status")
        || !capabilities.Contains("query.diplomacy.relation")
        || !capabilities.Contains("query.unit.status")
        || !capabilities.Contains("query.leader.status"))
    {
        std::cerr << "HOI3 capability registration incomplete\n";
        return 7;
    }
    if (!reverseProbes.Contains("hoi3.query.same_generation")
        || !reverseProbes.Contains(
            "hoi3.objects.unit_leader_generation"
        ))
    {
        std::cerr << "HOI3 reverse probe registration incomplete\n";
        return 17;
    }

    uint64_t assignedUnitKey = 0;
    uint64_t unassignedUnitKey = 0;
    uint64_t activeLeaderKey = 0;
    uint64_t reserveLeaderKey = 0;
    if (!core::PackHoi3UnitKey(
            AssignedUnitId0,
            AssignedUnitId1,
            assignedUnitKey
        )
        || !core::PackHoi3UnitKey(
            UnassignedUnitId0,
            UnassignedUnitId1,
            unassignedUnitKey
        )
        || !core::PackHoi3LeaderKey(
            ActiveLeaderId0,
            ActiveLeaderId1,
            activeLeaderKey
        )
        || !core::PackHoi3LeaderKey(
            ReserveLeaderId0,
            ReserveLeaderId1,
            reserveLeaderKey
        ))
    {
        std::cerr << "Unit/leader key packing failed\n";
        return 8;
    }

    core::engine::ObjectHandle handle;
    if (!resolvers.Resolve(
            {
                core::engine::TypeId::Unit,
                assignedUnitKey,
                "CHI"
            },
            handle,
            error
        )
        || handle.address != assignedUnit)
    {
        std::cerr << "Assigned unit resolution failed: " << error << '\n';
        return 9;
    }
    if (!resolvers.Resolve(
            {
                core::engine::TypeId::Unit,
                unassignedUnitKey,
                "CHI"
            },
            handle,
            error
        )
        || handle.address != unassignedUnit)
    {
        std::cerr << "Unassigned unit resolution failed: " << error << '\n';
        return 10;
    }
    if (!resolvers.Resolve(
            {
                core::engine::TypeId::Leader,
                activeLeaderKey,
                "CHI"
            },
            handle,
            error
        )
        || handle.address != activeLeader)
    {
        std::cerr << "Active leader resolution failed: " << error << '\n';
        return 11;
    }
    if (!resolvers.Resolve(
            {
                core::engine::TypeId::Leader,
                reserveLeaderKey,
                "CHI"
            },
            handle,
            error
        )
        || handle.address != reserveLeader)
    {
        std::cerr << "Reserve leader resolution failed: " << error << '\n';
        return 12;
    }

    queries.SetGameplayContext(true, "CHI", 1);
    core::NativeQueryRequest unitRequest;
    unitRequest.operation = "unit.status";
    unitRequest.arguments.emplace(
        "unit_id0",
        core::NativeQueryValue(static_cast<int64_t>(UnassignedUnitId0))
    );
    unitRequest.arguments.emplace(
        "unit_id1",
        core::NativeQueryValue(static_cast<int64_t>(UnassignedUnitId1))
    );
    const core::NativeQueryResult unitResult = queries.ExecuteImmediate(
        std::move(unitRequest),
        1,
        1
    );
    bool hasLeader = true;
    int64_t unitCountryIndex = -1;
    if (!unitResult.Succeeded()
        || !ReadBooleanField(unitResult.value, "has_leader", hasLeader)
        || hasLeader
        || !ReadIntegerField(
            unitResult.value,
            "country_index",
            unitCountryIndex
        )
        || unitCountryIndex != CountryIndex)
    {
        std::cerr << "Unassigned unit query failed: "
                  << unitResult.code << ' ' << unitResult.message << '\n';
        return 13;
    }

    core::NativeQueryRequest assignedUnitRequest;
    assignedUnitRequest.operation = "unit.status";
    assignedUnitRequest.arguments.emplace(
        "unit_id0",
        core::NativeQueryValue(static_cast<int64_t>(AssignedUnitId0))
    );
    assignedUnitRequest.arguments.emplace(
        "unit_id1",
        core::NativeQueryValue(static_cast<int64_t>(AssignedUnitId1))
    );
    const core::NativeQueryResult assignedUnitResult =
        queries.ExecuteImmediate(std::move(assignedUnitRequest), 1, 1);
    int64_t assignedLeaderId0 = 0;
    int64_t assignedLeaderId1 = 0;
    if (!assignedUnitResult.Succeeded()
        || !ReadBooleanField(
            assignedUnitResult.value,
            "has_leader",
            hasLeader
        )
        || !hasLeader
        || !ReadIntegerField(
            assignedUnitResult.value,
            "leader_id0",
            assignedLeaderId0
        )
        || !ReadIntegerField(
            assignedUnitResult.value,
            "leader_id1",
            assignedLeaderId1
        )
        || assignedLeaderId0 != ActiveLeaderId0
        || assignedLeaderId1 != ActiveLeaderId1)
    {
        std::cerr << "Assigned unit query failed: "
                  << assignedUnitResult.code << ' '
                  << assignedUnitResult.message << '\n';
        return 14;
    }

    core::NativeQueryRequest leaderRequest;
    leaderRequest.operation = "leader.status";
    leaderRequest.arguments.emplace(
        "leader_id0",
        core::NativeQueryValue(static_cast<int64_t>(ReserveLeaderId0))
    );
    leaderRequest.arguments.emplace(
        "leader_id1",
        core::NativeQueryValue(static_cast<int64_t>(ReserveLeaderId1))
    );
    const core::NativeQueryResult leaderResult = queries.ExecuteImmediate(
        std::move(leaderRequest),
        1,
        1
    );
    bool hasUnit = true;
    int64_t leaderId0 = 0;
    int64_t leaderId1 = 0;
    if (!leaderResult.Succeeded()
        || !ReadBooleanField(leaderResult.value, "has_unit", hasUnit)
        || hasUnit
        || !ReadIntegerField(
            leaderResult.value,
            "leader_id0",
            leaderId0
        )
        || !ReadIntegerField(
            leaderResult.value,
            "leader_id1",
            leaderId1
        )
        || leaderId0 != ReserveLeaderId0
        || leaderId1 != ReserveLeaderId1)
    {
        std::cerr << "Reserve leader query failed: "
                  << leaderResult.code << ' '
                  << leaderResult.message << '\n';
        return 15;
    }

    saveLoadBarrier.Start();
    core::NativeLifecycleSample sample;
    sample.available = true;
    sample.gameplay = true;
    sample.playerTag = "CHI";
    sample.gameStateAddress = base + 0x2000;
    sample.worldFingerprint = 99;
    sample.hasTotalDays = true;
    sample.totalDays = 100;
    saveLoadBarrier.Observe(sample);
    saveLoadBarrier.Observe(sample);
    saveLoadBarrier.Observe(sample);
    core::ReverseProbeContext probeContext;
    probeContext.engine = &engine;
    probeContext.saveLoadBarrier = &saveLoadBarrier;
    probeContext.queries = &queries;
    probeContext.objectResolvers = &resolvers;
    probeContext.capabilities = &capabilities;
    probeContext.lifecycle.runtimeActive = true;
    probeContext.lifecycle.phase = core::GamePhase::Gameplay;
    probeContext.lifecycle.generation = 1;
    probeContext.lifecycle.saveGeneration = 1;
    probeContext.lifecycle.playerTag = "CHI";
    probeContext.callerStateId = 1;
    probeContext.callerThreadId = 1;
    const core::ReverseProbeResult objectProbe = reverseProbes.Run(
        "hoi3.objects.unit_leader_generation",
        probeContext
    );
    if (!objectProbe.Succeeded())
    {
        std::cerr << "HOI3 unit/leader reverse probe failed: "
                  << objectProbe.message << '\n';
        return 18;
    }

    module.Shutdown();
    if (resolvers.HasResolver(core::engine::TypeId::TechnologyDefinition)
        || resolvers.HasResolver(core::engine::TypeId::Relation)
        || resolvers.HasResolver(core::engine::TypeId::Unit)
        || resolvers.HasResolver(core::engine::TypeId::Leader)
        || queries.HasHandler("technology.status")
        || queries.HasHandler("diplomacy.relation")
        || queries.HasHandler("unit.status")
        || queries.HasHandler("leader.status")
        || reverseProbes.Contains("hoi3.query.same_generation")
        || reverseProbes.Contains(
            "hoi3.objects.unit_leader_generation"
        ))
    {
        std::cerr << "HOI3 native provider shutdown incomplete\n";
        return 16;
    }

    VirtualFree(allocation, 0, MEM_RELEASE);
    std::cout << "HOI3 native query module: passed\n";
    return 0;
}
