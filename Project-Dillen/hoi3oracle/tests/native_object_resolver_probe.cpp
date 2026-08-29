#include <iostream>
#include <memory>
#include <string>

#include "native_object_resolver.h"

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
    engine.ObserveLifecycleGeneration(5);

    core::CapabilityRegistry capabilities;
    core::NativeObjectResolverService resolver;
    resolver.Configure(&engine, &capabilities);
    bool gateOpen = true;
    resolver.SetSafetyGate([&gateOpen]() -> std::shared_ptr<void>
    {
        return gateOpen
            ? std::static_pointer_cast<void>(std::make_shared<int>(1))
            : std::shared_ptr<void>{};
    });

    core::NativeObjectResolverDescriptor descriptor;
    descriptor.type = core::engine::TypeId::GameState;
    descriptor.name = "probe_game_state";
    descriptor.provider = "probe";
    if (!resolver.RegisterResolver(
            descriptor,
            [](const core::NativeObjectKey& key,
               const core::NativeObjectResolveContext& context,
               std::uintptr_t& address,
               std::string&)
            {
                address = 0x20000000
                    + static_cast<std::uintptr_t>(
                        context.lifecycleGeneration * 0x100
                    )
                    + static_cast<std::uintptr_t>(key.stableId)
                    + key.stableName.size();
                return true;
            },
            error
        ))
    {
        std::cerr << "Resolver registration failed: " << error << '\n';
        return 2;
    }

    core::engine::ObjectHandle handle;
    if (!resolver.Resolve(
            {core::engine::TypeId::GameState, 7, "named"},
            handle,
            error
        )
        || handle.lifecycleGeneration != 5
        || handle.address != 0x2000050C
        || handle.stableName != "named")
    {
        std::cerr << "Initial resolve failed: " << error << '\n';
        return 3;
    }
    engine.ObserveLifecycleGeneration(6);
    if (engine.IsHandleCurrent(handle))
    {
        std::cerr << "Stale handle remained current\n";
        return 4;
    }
    core::engine::ObjectHandle refreshed;
    if (!resolver.Refresh(handle, refreshed, error)
        || refreshed.lifecycleGeneration != 6
        || refreshed.address != 0x2000060C
        || refreshed.stableName != "named")
    {
        std::cerr << "Handle refresh failed: " << error << '\n';
        return 5;
    }
    gateOpen = false;
    if (resolver.Resolve(
            {core::engine::TypeId::GameState, 7},
            handle,
            error
        )
        || error != "native_object_resolver_barrier_closed")
    {
        std::cerr << "Closed barrier allowed resolution\n";
        return 6;
    }
    if (!capabilities.Contains("resolver.probe_game_state"))
    {
        std::cerr << "Resolver capability missing\n";
        return 7;
    }
    gateOpen = true;
    capabilities.Invalidate(
        "resolver.probe_game_state",
        "probe_capability_invalidated"
    );
    if (resolver.Resolve(
            {core::engine::TypeId::GameState, 7},
            handle,
            error
        )
        || error.find("native_object_resolver_capability_unavailable")
            != 0)
    {
        std::cerr << "Invalid resolver capability remained executable\n";
        return 8;
    }

    std::cout << "Native object resolver: passed\n";
    return 0;
}
