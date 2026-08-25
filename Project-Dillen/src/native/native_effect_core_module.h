#pragma once

#include <string_view>

#include "core_module.h"

class NativeEffectCoreModule final : public core::IModule
{
public:
    std::string_view Id() const override;
    int Priority() const override;

    bool Initialize(
        core::Services& services,
        std::string& error
    ) override;
    void OnLifecycleEvent(
        const core::LifecycleEvent& event
    ) override;
    void Tick(uint64_t nowMilliseconds) override;
    void Shutdown() override;

private:
    core::DiagnosticSink diagnostic_;
    core::NativeEffectService* service_ = nullptr;
    core::NativeSaveLoadBarrier* saveLoadBarrier_ = nullptr;
    core::CapabilityRegistry* capabilities_ = nullptr;
    bool initialized_ = false;
};
