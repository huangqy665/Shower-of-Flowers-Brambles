#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace core
{

class NativeEffectService;

class Hoi3GameplayEffects
{
public:
    Hoi3GameplayEffects();
    ~Hoi3GameplayEffects();

    Hoi3GameplayEffects(const Hoi3GameplayEffects&) = delete;
    Hoi3GameplayEffects& operator=(const Hoi3GameplayEffects&) = delete;

    bool RegisterHandlers(
        NativeEffectService& service,
        std::string& error
    );
    void UnregisterHandlers();
    std::vector<std::string> Tick();
    std::size_t ClearQueuedActions();
    bool IsSupportedExecutable() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}
