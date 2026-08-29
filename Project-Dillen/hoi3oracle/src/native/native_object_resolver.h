#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "capability_registry.h"
#include "engine_registry.h"

namespace core
{

struct NativeObjectKey
{
    engine::TypeId type = engine::TypeId::GameState;
    uint64_t stableId = 0;
    std::string stableName;
};

struct NativeObjectResolveContext
{
    engine::EngineRegistry& engine;
    uint64_t lifecycleGeneration = 0;
};

using NativeObjectResolveHandler = std::function<bool(
    const NativeObjectKey&,
    const NativeObjectResolveContext&,
    std::uintptr_t&,
    std::string&
)>;

using NativeObjectResolverSafetyGate =
    std::function<std::shared_ptr<void>()>;

struct NativeObjectResolverDescriptor
{
    engine::TypeId type = engine::TypeId::GameState;
    std::string name;
    std::string provider;
    std::vector<engine::SymbolId> requiredSymbols;
    std::vector<engine::TypeId> requiredTypes;
    std::vector<engine::FieldId> requiredFields;
};

class NativeObjectResolverService
{
public:
    void Configure(
        engine::EngineRegistry* engine,
        CapabilityRegistry* capabilities
    );
    void SetSafetyGate(NativeObjectResolverSafetyGate gate);

    bool RegisterResolver(
        NativeObjectResolverDescriptor descriptor,
        NativeObjectResolveHandler handler,
        std::string& error
    );
    bool UnregisterResolver(engine::TypeId type);
    std::size_t UnregisterProvider(std::string_view provider);
    bool HasResolver(engine::TypeId type) const;

    bool Resolve(
        const NativeObjectKey& key,
        engine::ObjectHandle& handle,
        std::string& error
    ) const;
    bool ResolveGuarded(
        const NativeObjectKey& key,
        const std::shared_ptr<void>& safetyLease,
        engine::ObjectHandle& handle,
        std::string& error
    ) const;
    bool Refresh(
        const engine::ObjectHandle& stale,
        engine::ObjectHandle& refreshed,
        std::string& error
    ) const;

private:
    struct Entry
    {
        NativeObjectResolverDescriptor descriptor;
        NativeObjectResolveHandler handler;
    };

    bool ResolveInternal(
        const NativeObjectKey& key,
        engine::ObjectHandle& handle,
        std::string& error
    ) const;

    mutable std::mutex mutex_;
    std::unordered_map<engine::TypeId, Entry> entries_;
    engine::EngineRegistry* engine_ = nullptr;
    CapabilityRegistry* capabilities_ = nullptr;
    NativeObjectResolverSafetyGate safetyGate_;
};

NativeObjectResolverService& GetNativeObjectResolverService();

}
