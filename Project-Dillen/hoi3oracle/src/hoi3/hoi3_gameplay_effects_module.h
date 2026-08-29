#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "core_module.h"

namespace core
{

class Hoi3GameplayEffects;
class NativeEffectService;

class Hoi3GameplayEffectsModule final : public IModule
{
public:
    Hoi3GameplayEffectsModule();
    ~Hoi3GameplayEffectsModule() override;

    std::string_view Id() const override;
    int Priority() const override;
    bool Initialize(Services& services, std::string& error) override;
    void OnLifecycleEvent(const LifecycleEvent& event) override;
    void Tick(uint64_t nowMilliseconds) override;
    void Shutdown() override;

private:
    DiagnosticSink diagnostic_;
    NativeEffectService* service_ = nullptr;
    std::unique_ptr<Hoi3GameplayEffects> effects_;
    bool writesAllowed_ = false;
};

}
