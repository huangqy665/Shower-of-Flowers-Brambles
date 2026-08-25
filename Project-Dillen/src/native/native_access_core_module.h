#pragma once

#include <string_view>

#include "core_module.h"

namespace core
{

class NativeAccessCoreModule final : public IModule
{
public:
    std::string_view Id() const override;
    int Priority() const override;
    bool Initialize(Services& services, std::string& error) override;
    void OnLifecycleEvent(const LifecycleEvent& event) override;
    void Tick(uint64_t nowMilliseconds) override;
    void Shutdown() override;

private:
    DiagnosticSink diagnostic_;
    engine::EngineRegistry* engine_ = nullptr;
    NativeSaveLoadBarrier* saveLoadBarrier_ = nullptr;
    NativeQueryService* queries_ = nullptr;
    NativeObjectResolverService* resolvers_ = nullptr;
    CapabilityRegistry* capabilities_ = nullptr;
};

}
