#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "core_hook_registry.h"
#include "core_lifecycle.h"

namespace core
{

using DiagnosticSink = std::function<void(std::string_view)>;

class NativeEffectService;
class NativeQueryService;
class NativeObjectResolverService;
class CapabilityRegistry;
class NativeSaveLoadBarrier;
class ReverseProbeFramework;
namespace engine
{
class EngineRegistry;
}

struct Services
{
    HookRegistry& hooks;
    LifecycleService& lifecycle;
    DiagnosticSink diagnostic;
    NativeEffectService* effects = nullptr;
    engine::EngineRegistry* engine = nullptr;
    NativeSaveLoadBarrier* saveLoadBarrier = nullptr;
    ReverseProbeFramework* reverseProbes = nullptr;
    std::function<bool(
        std::string_view,
        LifecycleEventSource
    )> notifySaveLoadStarted;
    std::function<bool(
        std::string_view,
        LifecycleEventSource
    )> notifySaveLoaded;
    NativeQueryService* queries = nullptr;
    NativeObjectResolverService* objectResolvers = nullptr;
    CapabilityRegistry* capabilities = nullptr;
};

class IModule
{
public:
    virtual ~IModule() = default;

    virtual std::string_view Id() const = 0;
    virtual int Priority() const
    {
        return 0;
    }

    virtual bool Initialize(
        Services& services,
        std::string& error
    ) = 0;

    virtual void OnLifecycleEvent(
        const LifecycleEvent& event
    ) = 0;

    virtual void Tick(uint64_t nowMilliseconds) = 0;
    virtual void Shutdown() = 0;
};

}
