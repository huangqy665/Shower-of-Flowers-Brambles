#pragma once

#include <cstdint>
#include <mutex>
#include <string>

#include "core_module.h"
#include "reverse_probe_framework.h"

namespace core
{

class Hoi3NativeQueriesModule final : public IModule
{
public:
    std::string_view Id() const override;
    int Priority() const override;
    bool Initialize(Services& services, std::string& error) override;
    void OnLifecycleEvent(const LifecycleEvent& event) override;
    void Tick(uint64_t nowMilliseconds) override;
    void Shutdown() override;

private:
    bool RegisterResolvers(std::string& error);
    bool RegisterQueries(std::string& error);
    bool RegisterReverseProbes(std::string& error);
    ReverseProbeResult ProbeSameGenerationSnapshot(
        const ReverseProbeContext& context
    );
    ReverseProbeResult ProbeUnitLeaderGeneration(
        const ReverseProbeContext& context
    );

    struct ObjectProbeBaseline
    {
        bool valid = false;
        std::string playerTag;
        uint64_t saveGeneration = 0;
        uint64_t unitKey = 0;
        uint64_t activeLeaderKey = 0;
        uint64_t reserveLeaderKey = 0;
        std::uintptr_t unitAddress = 0;
        std::uintptr_t activeLeaderAddress = 0;
        std::uintptr_t reserveLeaderAddress = 0;
    };

    DiagnosticSink diagnostic_;
    engine::EngineRegistry* engine_ = nullptr;
    NativeQueryService* queries_ = nullptr;
    NativeObjectResolverService* resolvers_ = nullptr;
    ReverseProbeFramework* reverseProbes_ = nullptr;
    std::mutex objectProbeMutex_;
    ObjectProbeBaseline objectProbeBaseline_;
};

}
