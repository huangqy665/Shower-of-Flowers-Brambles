#include "native_object_resolver.h"

#include <algorithm>
#include <utility>

namespace core
{

void NativeObjectResolverService::Configure(
    engine::EngineRegistry* engine,
    CapabilityRegistry* capabilities
)
{
    std::lock_guard<std::mutex> lock(mutex_);
    engine_ = engine;
    capabilities_ = capabilities;
}

void NativeObjectResolverService::SetSafetyGate(
    NativeObjectResolverSafetyGate gate
)
{
    std::lock_guard<std::mutex> lock(mutex_);
    safetyGate_ = std::move(gate);
}

bool NativeObjectResolverService::RegisterResolver(
    NativeObjectResolverDescriptor descriptor,
    NativeObjectResolveHandler handler,
    std::string& error
)
{
    descriptor.name = NormalizeCapabilityId(descriptor.name);
    descriptor.provider = NormalizeCapabilityId(descriptor.provider);
    if (descriptor.name.empty() || descriptor.provider.empty() || !handler)
    {
        error = "native_object_resolver_invalid";
        return false;
    }
    CapabilityRegistry* capabilities = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (entries_.find(descriptor.type) != entries_.end())
        {
            error = "native_object_resolver_duplicate: " + descriptor.name;
            return false;
        }
        capabilities = capabilities_;
        entries_.emplace(
            descriptor.type,
            Entry{descriptor, std::move(handler)}
        );
    }
    if (capabilities)
    {
        CapabilityDescriptor capability;
        capability.id = "resolver." + descriptor.name;
        capability.provider = descriptor.provider;
        capability.kind = CapabilityKind::ObjectResolver;
        capability.access = CapabilityAccess::Read;
        capability.rollback = CapabilityRollback::NotApplicable;
        capability.persistence = CapabilityPersistence::None;
        capability.multiplayer = CapabilityMultiplayer::Deterministic;
        capability.requiredSymbols = descriptor.requiredSymbols;
        capability.requiredFields = descriptor.requiredFields;
        capability.requiredTypes = descriptor.requiredTypes;
        if (std::find(
                capability.requiredTypes.begin(),
                capability.requiredTypes.end(),
                descriptor.type
            ) == capability.requiredTypes.end())
        {
            capability.requiredTypes.push_back(descriptor.type);
        }
        if (!capabilities->Register(std::move(capability), error))
        {
            std::lock_guard<std::mutex> lock(mutex_);
            entries_.erase(descriptor.type);
            return false;
        }
    }
    error.clear();
    return true;
}

bool NativeObjectResolverService::UnregisterResolver(engine::TypeId type)
{
    std::string capabilityId;
    CapabilityRegistry* capabilities = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = entries_.find(type);
        if (found == entries_.end())
        {
            return false;
        }
        capabilityId = "resolver." + found->second.descriptor.name;
        capabilities = capabilities_;
        entries_.erase(found);
    }
    if (capabilities)
    {
        capabilities->Unregister(capabilityId);
    }
    return true;
}

std::size_t NativeObjectResolverService::UnregisterProvider(
    std::string_view provider
)
{
    const std::string normalized = NormalizeCapabilityId(provider);
    std::vector<engine::TypeId> types;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : entries_)
        {
            if (entry.second.descriptor.provider == normalized)
            {
                types.push_back(entry.first);
            }
        }
    }
    for (const engine::TypeId type : types)
    {
        UnregisterResolver(type);
    }
    return types.size();
}

bool NativeObjectResolverService::HasResolver(engine::TypeId type) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return entries_.find(type) != entries_.end();
}

bool NativeObjectResolverService::Resolve(
    const NativeObjectKey& key,
    engine::ObjectHandle& handle,
    std::string& error
) const
{
    NativeObjectResolverSafetyGate gate;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        gate = safetyGate_;
    }
    std::shared_ptr<void> lease = gate ? gate() : std::shared_ptr<void>{};
    if (gate && !lease)
    {
        error = "native_object_resolver_barrier_closed";
        return false;
    }
    return ResolveInternal(key, handle, error);
}

bool NativeObjectResolverService::ResolveGuarded(
    const NativeObjectKey& key,
    const std::shared_ptr<void>& safetyLease,
    engine::ObjectHandle& handle,
    std::string& error
) const
{
    if (!safetyLease)
    {
        error = "native_object_resolver_lease_missing";
        return false;
    }
    return ResolveInternal(key, handle, error);
}

bool NativeObjectResolverService::Refresh(
    const engine::ObjectHandle& stale,
    engine::ObjectHandle& refreshed,
    std::string& error
) const
{
    return Resolve(
        {stale.type, stale.stableId, stale.stableName},
        refreshed,
        error
    );
}

bool NativeObjectResolverService::ResolveInternal(
    const NativeObjectKey& key,
    engine::ObjectHandle& handle,
    std::string& error
) const
{
    handle = {};
    Entry entry;
    engine::EngineRegistry* engine = nullptr;
    CapabilityRegistry* capabilities = nullptr;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = entries_.find(key.type);
        if (found == entries_.end())
        {
            error = "native_object_resolver_missing";
            return false;
        }
        entry = found->second;
        engine = engine_;
        capabilities = capabilities_;
    }
    if (!engine || !engine->IsActive())
    {
        error = "native_object_resolver_engine_inactive";
        return false;
    }
    if (capabilities)
    {
        const auto capability = capabilities->Query(
            "resolver." + entry.descriptor.name,
            engine
        );
        if (!capability || !capability->Available())
        {
            error = capability
                ? "native_object_resolver_capability_unavailable: "
                    + capability->reason
                : "native_object_resolver_capability_missing";
            return false;
        }
    }
    const uint64_t generation = engine->LifecycleGeneration();
    if (generation == 0)
    {
        error = "native_object_resolver_lifecycle_unavailable";
        return false;
    }
    std::uintptr_t address = 0;
    NativeObjectResolveContext context{*engine, generation};
    if (!entry.handler(key, context, address, error) || !address)
    {
        if (error.empty())
        {
            error = "native_object_not_found";
        }
        return false;
    }
    if (engine->LifecycleGeneration() != generation)
    {
        error = "native_object_resolver_lifecycle_changed";
        return false;
    }
    handle = engine->MakeHandle(key.type, key.stableId, address);
    handle.stableName = key.stableName;
    if (!engine->IsHandleCurrent(handle))
    {
        handle = {};
        error = "native_object_resolver_handle_stale";
        return false;
    }
    error.clear();
    return true;
}

NativeObjectResolverService& GetNativeObjectResolverService()
{
    static NativeObjectResolverService service;
    return service;
}

}
