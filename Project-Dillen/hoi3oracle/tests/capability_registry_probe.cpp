#include <iostream>
#include <string>

#include "capability_registry.h"

int main()
{
    core::engine::EngineRegistry engine;
    const auto& profile = core::engine::Hoi3Tfh402D328Profile();
    std::string error;
    if (!engine.SelectVersion(profile.version.executable, 0x10000000, error))
    {
        std::cerr << "Engine setup failed: " << error << '\n';
        return 1;
    }

    core::CapabilityRegistry registry;
    if (!registry.SynchronizeEngineProfile(engine, error))
    {
        std::cerr << "Engine capability import failed: " << error << '\n';
        return 2;
    }
    const auto field = registry.Query(
        "engine.field.country.manpower",
        &engine
    );
    if (!field || !field->Available()
        || field->descriptor.access != core::CapabilityAccess::Write)
    {
        std::cerr << "Engine field capability unavailable\n";
        return 3;
    }

    core::CapabilityDescriptor descriptor;
    descriptor.id = "query.probe.echo";
    descriptor.provider = "probe";
    descriptor.kind = core::CapabilityKind::NativeQuery;
    descriptor.access = core::CapabilityAccess::Read;
    if (!registry.Register(std::move(descriptor), error))
    {
        std::cerr << "Capability registration failed: " << error << '\n';
        return 4;
    }
    const auto available = registry.Query("QUERY.PROBE.ECHO", &engine);
    if (!available || !available->Available())
    {
        std::cerr << "Registered capability unavailable\n";
        return 5;
    }
    if (!registry.Invalidate("query.probe.echo", "probe_invalidation"))
    {
        std::cerr << "Capability invalidation failed\n";
        return 6;
    }
    const auto invalid = registry.Query("query.probe.echo", &engine);
    if (!invalid
        || invalid->availability != core::CapabilityAvailability::Invalid
        || invalid->reason != "probe_invalidation")
    {
        std::cerr << "Invalid capability remained available\n";
        return 7;
    }
    if (registry.UnregisterProvider("engine_registry") == 0
        || registry.Contains("engine.field.country.manpower"))
    {
        std::cerr << "Provider removal failed\n";
        return 8;
    }

    std::cout << "Capability registry: passed\n";
    return 0;
}
