#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

#include "core_module.h"

namespace core
{

class NativeSaveLoadCoreModule final : public IModule
{
public:
    std::string_view Id() const override;
    int Priority() const override;

    bool Initialize(Services& services, std::string& error) override;
    void OnLifecycleEvent(const LifecycleEvent& event) override;
    void Tick(uint64_t nowMilliseconds) override;
    void Shutdown() override;

private:
    using NativeLoadFunction = void (__stdcall *)(void*, void*);

    static void __stdcall LoadProxy(void* gameState, void* source);
    static bool HookDisabled();

    bool Install(std::string& error);
    void Uninstall();
    bool IsInstalled() const;
    void NotifyStarted(std::string_view key) const;
    void NotifyCompleted(std::string_view key) const;

    engine::EngineRegistry* engine_ = nullptr;
    DiagnosticSink diagnostic_;
    std::function<bool(
        std::string_view,
        LifecycleEventSource
    )> notifyStarted_;
    std::function<bool(
        std::string_view,
        LifecycleEventSource
    )> notifyCompleted_;
    std::uintptr_t callSite_ = 0;
    NativeLoadFunction original_ = nullptr;
    uint8_t originalBytes_[5]{};
    uint8_t patchedBytes_[5]{};
    std::atomic<uint64_t> sequence_{0};
    std::atomic<bool> installed_{false};

    static std::atomic<NativeSaveLoadCoreModule*> active_;
};

}
