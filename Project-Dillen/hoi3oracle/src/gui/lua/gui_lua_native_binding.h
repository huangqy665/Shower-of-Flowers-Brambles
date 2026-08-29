#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "gui_lua_bridge.h"
#include "scripted_gui_overlay_api.h"

namespace core
{
struct ReverseProbeReport;
}

using GuiReverseProbeRunner = std::function<bool(
    const std::vector<std::string>&,
    uint64_t,
    uint64_t,
    core::ReverseProbeReport&,
    std::string&
)>;

class GuiLuaNativeBinding
{
public:
    GuiLuaNativeBinding();
    ~GuiLuaNativeBinding();

    GuiLuaNativeBinding(const GuiLuaNativeBinding&) = delete;
    GuiLuaNativeBinding& operator=(const GuiLuaNativeBinding&) = delete;

    bool Install(
        ScriptedGuiLuaState* state,
        const ScriptedGuiLua51ApiV1& api,
        GuiLuaBridgeService& service,
        std::string& error
    );

    bool DetachState(ScriptedGuiLuaState* state);
    bool TouchState(ScriptedGuiLuaState* state);
    std::vector<ScriptedGuiLuaState*> PruneInactiveStates(
        uint64_t maximumIdleMilliseconds
    );
    void ResetChannelOwnership();
    void DetachAll();
    bool IsInstalled() const;
    bool IsStateInstalled(ScriptedGuiLuaState* state) const;
    std::size_t StateCount() const;
    void SetReverseProbeRunner(GuiReverseProbeRunner runner);

private:
    static int __cdecl TryAcquireChannelThunk(
        ScriptedGuiLuaState* state
    );
    static int __cdecl ReleaseChannelThunk(
        ScriptedGuiLuaState* state
    );
    static int __cdecl PublishUpdateThunk(ScriptedGuiLuaState* state);
    static int __cdecl TryPopActionThunk(ScriptedGuiLuaState* state);
    static int __cdecl ExecuteEffectsThunk(ScriptedGuiLuaState* state);
    static int __cdecl HasEffectThunk(ScriptedGuiLuaState* state);
    static int __cdecl QueryThunk(ScriptedGuiLuaState* state);
    static int __cdecl QuerySnapshotThunk(ScriptedGuiLuaState* state);
    static int __cdecl RunReverseProbesThunk(
        ScriptedGuiLuaState* state
    );
    static int __cdecl HasQueryThunk(ScriptedGuiLuaState* state);
    static int __cdecl GetCapabilityThunk(ScriptedGuiLuaState* state);

    int TryAcquireChannel(
        ScriptedGuiLuaState* state,
        const ScriptedGuiLua51ApiV1& api,
        GuiLuaBridgeService& service
    );
    int ReleaseChannel(
        ScriptedGuiLuaState* state,
        const ScriptedGuiLua51ApiV1& api,
        GuiLuaBridgeService& service
    );
    int PublishUpdate(
        ScriptedGuiLuaState* state,
        const ScriptedGuiLua51ApiV1& api,
        GuiLuaBridgeService& service
    );
    int TryPopAction(
        ScriptedGuiLuaState* state,
        const ScriptedGuiLua51ApiV1& api,
        GuiLuaBridgeService& service
    );
    int ExecuteEffects(
        ScriptedGuiLuaState* state,
        const ScriptedGuiLua51ApiV1& api,
        uint64_t stateOrdinal
    );
    int HasEffect(
        ScriptedGuiLuaState* state,
        const ScriptedGuiLua51ApiV1& api
    );
    int Query(
        ScriptedGuiLuaState* state,
        const ScriptedGuiLua51ApiV1& api,
        uint64_t stateOrdinal
    );
    int QuerySnapshot(
        ScriptedGuiLuaState* state,
        const ScriptedGuiLua51ApiV1& api,
        uint64_t stateOrdinal
    );
    int RunReverseProbes(
        ScriptedGuiLuaState* state,
        const ScriptedGuiLua51ApiV1& api,
        uint64_t stateOrdinal
    );
    int HasQuery(
        ScriptedGuiLuaState* state,
        const ScriptedGuiLua51ApiV1& api
    );
    int GetCapability(
        ScriptedGuiLuaState* state,
        const ScriptedGuiLua51ApiV1& api
    );

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

GuiLuaNativeBinding& GetGuiLuaNativeBinding();
