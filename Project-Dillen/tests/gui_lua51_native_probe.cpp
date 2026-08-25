#include <windows.h>

#include <filesystem>
#include <iostream>
#include <string>

#include "gui_lua51_hook.h"
#include "gui_lua_bridge.h"
#include "gui_lua_native_binding.h"
#include "gui_diagnostics.h"
#include "capability_registry.h"
#include "native_effect_bridge.h"
#include "native_query_service.h"
#include "reverse_probe_framework.h"

namespace
{

using LuaNewStateFunction = ScriptedGuiLuaState* (__cdecl *)();
using LuaOpenLibrariesFunction = void (__cdecl *)(ScriptedGuiLuaState*);
using LuaLoadStringFunction = int (__cdecl *)(
    ScriptedGuiLuaState*,
    const char*
);
using LuaPCallFunction = int (__cdecl *)(
    ScriptedGuiLuaState*,
    int,
    int,
    int
);
using LuaCloseFunction = void (__cdecl *)(ScriptedGuiLuaState*);

template <typename Function>
Function Resolve(HMODULE module, const char* name)
{
    return reinterpret_cast<Function>(GetProcAddress(module, name));
}

bool RunBooleanChunk(
    ScriptedGuiLuaState* state,
    const ScriptedGuiLua51ApiV1& api,
    LuaLoadStringFunction loadString,
    LuaPCallFunction pcall,
    const char* source
)
{
    api.setTop(state, 0);
    if (loadString(state, source) != 0
        || pcall(state, 0, 1, 0) != 0)
    {
        return false;
    }
    return api.type(state, -1) == 1
        && api.toBoolean(state, -1) != 0;
}

}

int wmain(int argumentCount, wchar_t** arguments)
{
    SetGuiDiagnosticsRoot(
        std::filesystem::temp_directory_path()
            / "scripted_gui_native_probe"
    );
    if (argumentCount != 2
        || arguments[1][0] == L'\0'
        || !std::filesystem::is_regular_file(arguments[1]))
    {
        std::cout << "Lua 5.1 native probe skipped: DLL unavailable\n";
        return 77;
    }
    const std::filesystem::path luaPath = arguments[1];
    SetDllDirectoryW(luaPath.parent_path().wstring().c_str());
    HMODULE module = LoadLibraryW(luaPath.wstring().c_str());
    if (!module)
    {
        std::cerr << "failed to load Lua DLL\n";
        return 3;
    }

    const LuaNewStateFunction newState =
        Resolve<LuaNewStateFunction>(module, "luaL_newstate");
    const LuaOpenLibrariesFunction openLibraries =
        Resolve<LuaOpenLibrariesFunction>(module, "luaL_openlibs");
    const LuaLoadStringFunction loadString =
        Resolve<LuaLoadStringFunction>(module, "luaL_loadstring");
    const LuaPCallFunction pcall =
        Resolve<LuaPCallFunction>(module, "lua_pcall");
    const LuaCloseFunction closeState =
        Resolve<LuaCloseFunction>(module, "lua_close");
    if (!newState || !openLibraries || !loadString || !pcall || !closeState)
    {
        std::cerr << "required Lua exports are missing\n";
        FreeLibrary(module);
        return 4;
    }

    ScriptedGuiLua51ApiV1 api;
    std::string error;
    if (!ResolveGuiLua51Api(api, error))
    {
        std::cerr << error << '\n';
        FreeLibrary(module);
        return 5;
    }
    ScriptedGuiLuaState* state = newState();
    if (!state)
    {
        std::cerr << "luaL_newstate failed\n";
        FreeLibrary(module);
        return 6;
    }
    openLibraries(state);

    if (!RunBooleanChunk(
            state,
            api,
            loadString,
            pcall,
            "CCurrentGameState={"
            "GetPlayer=function() return 'CHI' end,"
            "GetCurrentDate=function() return {"
            "GetTotalDays=function() return 321 end} end};"
            "return true"
        ))
    {
        std::cerr << "Lua lifecycle fixture setup failed\n";
        closeState(state);
        FreeLibrary(module);
        return 19;
    }
    const int lifecycleStackTop = api.getTop(state);
    GuiLua51LifecycleObservation lifecycle;
    std::string lifecycleError;
    if (!ProbeGuiLua51LifecycleState(
            state,
            lifecycle,
            lifecycleError
        )
        || !lifecycle.playerQuerySucceeded
        || lifecycle.playerTag != "CHI"
        || api.getTop(state) != lifecycleStackTop)
    {
        std::cerr << "Lua lifecycle probe failed: "
                  << lifecycleError << '\n';
        closeState(state);
        FreeLibrary(module);
        return 20;
    }
    api.setTop(state, 0);

    GuiLuaBridgeService service;
    std::unique_ptr<IGuiDataBridgeChannel> channel =
        service.CreateChannel("probe", {});
    const std::filesystem::path root;
    if (!channel
        || !channel->Open(GuiDataProviderInitContext{root}, error)
        || !GetGuiLuaNativeBinding().Install(
            state,
            api,
            service,
            error
        ))
    {
        std::cerr << error << '\n';
        GetGuiLuaNativeBinding().DetachAll();
        closeState(state);
        FreeLibrary(module);
        return 7;
    }
    int64_t nativeEffectTotal = 0;
    core::NativeEffectService& nativeEffects =
        core::GetNativeEffectService();
    nativeEffects.SetGameplayContext(true, "CHI", 1);
    if (!nativeEffects.RegisterHandler(
            "probe.add",
            [&nativeEffectTotal](
                const core::NativeEffect& effect,
                const core::NativeEffectExecutionContext&,
                core::PreparedNativeEffect& prepared,
                std::string& handlerError
            )
            {
                int64_t amount = 0;
                const core::NativeEffectValue* value =
                    effect.Find("amount");
                if (!value
                    || !core::NativeEffectValueToInteger(
                        *value,
                        amount
                    ))
                {
                    handlerError = "probe_amount_invalid";
                    return false;
                }
                prepared.apply = [
                    &nativeEffectTotal,
                    amount
                ](std::string& applyError)
                {
                    nativeEffectTotal += amount;
                    applyError.clear();
                    return true;
                };
                prepared.rollback = [&nativeEffectTotal, amount]
                {
                    nativeEffectTotal -= amount;
                };
                handlerError.clear();
                return true;
            },
            error
        )
        || !RunBooleanChunk(
            state,
            api,
            loadString,
            pcall,
            "local ok,code,message,transaction="
            "NewCoreNative.ExecuteEffects({source='probe',atomic=true,"
            "effects={{operation='probe.add',amount=2},"
            "{effect='probe.add',arguments={amount=3,"
            "provinceIds={1,2,3}}}}});"
            "return NewCoreNative.HasEffect('PROBE.ADD') and ok "
            "and code=='ok' and message=='' and transaction>0"
        )
        || nativeEffectTotal != 5)
    {
        std::cerr << "Generic native effect Lua bridge failed: "
                  << error << '\n';
        nativeEffects.UnregisterHandler("probe.add");
        GetGuiLuaNativeBinding().DetachAll();
        closeState(state);
        FreeLibrary(module);
        return 21;
    }
    core::NativeQueryService& nativeQueries =
        core::GetNativeQueryService();
    nativeQueries.Configure(&core::GetCapabilityRegistry(), nullptr);
    nativeQueries.SetGameplayContext(true, "CHI", 1);
    core::NativeQueryDescriptor queryDescriptor;
    queryDescriptor.operation = "probe.read";
    queryDescriptor.provider = "probe";
    if (!nativeQueries.RegisterHandler(
            std::move(queryDescriptor),
            [](
                const core::NativeQueryRequest& request,
                const core::NativeQueryExecutionContext& context,
                core::NativeQueryValue& output,
                std::string& queryError
            )
            {
                int64_t amount = 0;
                const core::NativeQueryValue* argument =
                    request.Find("amount");
                if (!argument
                    || !core::NativeQueryValueToInteger(
                        *argument,
                        amount
                    ))
                {
                    queryError = "probe_query_amount_invalid";
                    return false;
                }
                output = core::NativeQueryValue::Object({
                    {"amount", core::NativeQueryValue(amount * 2)},
                    {"player", core::NativeQueryValue(context.playerTag)}
                });
                queryError.clear();
                return true;
            },
            error
        )
        || !RunBooleanChunk(
            state,
            api,
            loadString,
            pcall,
            "local ok,value,code,message="
            "NewCoreNative.Query('PROBE.READ',{amount=7});"
            "local capability=NewCoreNative.GetCapability("
            "'query.probe.read');"
            "return NewCoreNative.HasQuery('probe.read') and ok "
            "and value.amount==14 and value.player=='CHI' "
            "and code=='ok' and message=='' "
            "and capability~=nil and capability.available "
            "and capability.kind=='native_query' "
            "and capability.access=='read'"
        )
        || !RunBooleanChunk(
            state,
            api,
            loadString,
            pcall,
            "local ok,snapshot,code,message="
            "NewCoreNative.QuerySnapshot({"
            "{key='first',operation='probe.read',"
            "arguments={amount=2}},"
            "{key='second',operation='probe.read',"
            "arguments={amount=5}}});"
            "return ok and code=='ok' and message=='' "
            "and snapshot.snapshot_id>0 and snapshot.generation==1 "
            "and snapshot.player_tag=='CHI' "
            "and snapshot.values.first.amount==4 "
            "and snapshot.values.second.amount==10 "
            "and snapshot.results.first.ok "
            "and snapshot.results.second.operation=='probe.read'"
        ))
    {
        std::cerr << "Generic native query Lua bridge failed: "
                  << error << '\n';
        nativeQueries.UnregisterProvider("probe");
        nativeEffects.UnregisterHandler("probe.add");
        GetGuiLuaNativeBinding().DetachAll();
        closeState(state);
        FreeLibrary(module);
        return 22;
    }
    GetGuiLuaNativeBinding().SetReverseProbeRunner(
        [](
            const std::vector<std::string>& ids,
            uint64_t callerStateId,
            uint64_t callerThreadId,
            core::ReverseProbeReport& report,
            std::string& runnerError
        )
        {
            if (ids.size() != 2
                || ids[0] != "probe.first"
                || ids[1] != "probe.second"
                || callerStateId == 0
                || callerThreadId == 0)
            {
                runnerError = "reverse_probe_test_request_invalid";
                return false;
            }
            report.runId = 17;
            report.timestampMilliseconds = 123;
            report.lifecycleGeneration = 4;
            report.barrierGeneration = 5;
            report.playerTag = "CHI";
            for (const std::string& id : ids)
            {
                core::ReverseProbeResult result;
                result.id = id;
                result.category = "self_test";
                result.access = core::ReverseProbeAccess::ReadMemory;
                result.status = core::ReverseProbeStatus::Passed;
                result.evidence = core::ReverseProbeEvidence::Proven;
                result.durationMicroseconds = 7;
                result.version = "probe";
                result.message = "passed";
                report.results.push_back(std::move(result));
            }
            runnerError.clear();
            return true;
        }
    );
    if (!RunBooleanChunk(
            state,
            api,
            loadString,
            pcall,
            "local ok,report,code,message="
            "NewCoreNative.RunReverseProbes({"
            "'probe.first','probe.second'});"
            "return ok and code=='' and message=='' "
            "and report.run_id==17 "
            "and report.lifecycle_generation==4 "
            "and report.barrier_generation==5 "
            "and report.player_tag=='CHI' "
            "and report.results['probe.first'].status=='passed' "
            "and report.results['probe.second'].evidence=='proven'"
        ))
    {
        std::cerr << "Generic reverse probe Lua bridge failed\n";
        GetGuiLuaNativeBinding().SetReverseProbeRunner({});
        nativeQueries.UnregisterProvider("probe");
        nativeEffects.UnregisterHandler("probe.add");
        GetGuiLuaNativeBinding().DetachAll();
        closeState(state);
        FreeLibrary(module);
        return 23;
    }
    if (!RunBooleanChunk(
            state,
            api,
            loadString,
            pcall,
            "local ok,status=ScriptedGuiNative.TryAcquireChannel("
            "'probe',10);return ok and status=='claimed'"
        ))
    {
        std::cerr << "Initial Lua publisher reservation failed\n";
        GetGuiLuaNativeBinding().DetachAll();
        closeState(state);
        FreeLibrary(module);
        return 8;
    }

    const char* publishSource =
        "return ScriptedGuiNative.PublishUpdate('probe',{"
        "revision=1,baseRevision=0,fullSnapshot=true,"
        "values={['state.visible']=true,"
        "['state.viewertag']='CHI',"
        "['regions.7.controlledPercentage']=91.5},"
        "removedValues={},"
        "lists={assigned_leader_list={revision=4,items={{"
        "id=11,text='leader',regionid=7,x=0.25,y=0.5}}}},"
        "removedLists={}})";
    GuiDataBridgeUpdate update;
    if (!RunBooleanChunk(
            state,
            api,
            loadString,
            pcall,
            publishSource
        )
        || channel->Poll(update, error) != GuiDataBridgePollResult::Update)
    {
        std::cerr << "Lua update publication failed: " << error << '\n';
        GetGuiLuaNativeBinding().DetachAll();
        closeState(state);
        FreeLibrary(module);
        return 9;
    }
    const auto visible = update.values.find("state.visible");
    const auto percentage = update.values.find(
        "regions.7.controlledpercentage"
    );
    const auto leaders = update.lists.find("assigned_leader_list");
    const GuiGameplayLifecycleSnapshot gameplayLifecycle =
        service.GameplayLifecycle();
    if (update.revision != 1
        || !update.fullSnapshot
        || visible == update.values.end()
        || !GuiDataValueToBool(visible->second)
        || percentage == update.values.end()
        || GuiDataValueToNumber(percentage->second) != 91.5
        || leaders == update.lists.end()
        || leaders->second.revision != 4
        || leaders->second.items.size() != 1
        || leaders->second.items.front().id != 11
        || !leaders->second.items.front().Find("regionid")
        || GuiDataValueToNumber(
            *leaders->second.items.front().Find("regionid")
        ) != 7.0
        || gameplayLifecycle.state
            != GuiGameplayLifecycleState::Gameplay
        || gameplayLifecycle.playerTag != "CHI")
    {
        std::cerr << "decoded Lua update did not match\n";
        GetGuiLuaNativeBinding().DetachAll();
        closeState(state);
        FreeLibrary(module);
        return 10;
    }

    ScriptedGuiLuaState* secondState = newState();
    if (!secondState)
    {
        std::cerr << "second luaL_newstate failed\n";
        GetGuiLuaNativeBinding().DetachAll();
        closeState(state);
        FreeLibrary(module);
        return 11;
    }
    openLibraries(secondState);
    if (!GetGuiLuaNativeBinding().Install(
            secondState,
            api,
            service,
            error
        )
        || !RunBooleanChunk(
            secondState,
            api,
            loadString,
            pcall,
            "local ok,status=ScriptedGuiNative.TryAcquireChannel("
            "'probe',10);return not ok and status=='rejected'"
        )
        || !RunBooleanChunk(
            secondState,
            api,
            loadString,
            pcall,
            "return ScriptedGuiNative.PublishUpdate('probe',{"
            "revision=2,baseRevision=0,fullSnapshot=true,"
            "values={['state.visible']=false},removedValues={},"
            "lists={},removedLists={}})==false"
        ))
    {
        std::cerr << "Competing Lua publisher was not rejected: "
                  << error << '\n';
        GetGuiLuaNativeBinding().DetachAll();
        closeState(secondState);
        closeState(state);
        FreeLibrary(module);
        return 11;
    }
    GuiDataBridgeUpdate rejectedUpdate;
    if (channel->Poll(rejectedUpdate, error)
        != GuiDataBridgePollResult::Empty)
    {
        std::cerr << "Rejected publisher queued an update\n";
        GetGuiLuaNativeBinding().DetachAll();
        closeState(secondState);
        closeState(state);
        FreeLibrary(module);
        return 12;
    }

    if (!RunBooleanChunk(
            secondState,
            api,
            loadString,
            pcall,
            "local ok,status=ScriptedGuiNative.TryAcquireChannel("
            "'probe',100);return ok and status=='taken_over'"
        )
        || !RunBooleanChunk(
            secondState,
            api,
            loadString,
            pcall,
            "return ScriptedGuiNative.PublishUpdate('probe',{"
            "revision=1,baseRevision=0,fullSnapshot=true,"
            "values={['state.visible']=false,"
            "['state.viewertag']='CHI'},removedValues={},"
            "lists={},removedLists={}})"
        ))
    {
        std::cerr << "Higher-priority Lua publisher takeover failed\n";
        GetGuiLuaNativeBinding().DetachAll();
        closeState(secondState);
        closeState(state);
        FreeLibrary(module);
        return 13;
    }
    GuiDataBridgeUpdate takeoverUpdate;
    if (channel->Poll(takeoverUpdate, error)
            != GuiDataBridgePollResult::Update
        || takeoverUpdate.revision != 2
        || !takeoverUpdate.fullSnapshot)
    {
        std::cerr << "Takeover snapshot was not globally sequenced\n";
        GetGuiLuaNativeBinding().DetachAll();
        closeState(secondState);
        closeState(state);
        FreeLibrary(module);
        return 14;
    }

    GuiActionContext action;
    action.action = "move_leader";
    action.functionName = "MoveLeader";
    action.parameters["regionid"] = "7";
    if (channel->SendAction(action, error)
            != GuiDataBridgeSendResult::Accepted
        || !RunBooleanChunk(
            state,
            api,
            loadString,
            pcall,
            "return ScriptedGuiNative.TryPopAction('probe')==nil"
        )
        || !RunBooleanChunk(
            secondState,
            api,
            loadString,
            pcall,
            "local a=ScriptedGuiNative.TryPopAction('probe');"
            "return a and a.functionName=='MoveLeader' "
            "and a.regionid=='7' and a.parameters.regionid=='7'"
        ))
    {
        std::cerr << "Lua action delivery failed: " << error << '\n';
        GetGuiLuaNativeBinding().DetachAll();
        closeState(secondState);
        closeState(state);
        FreeLibrary(module);
        return 15;
    }

    if (!GetGuiLuaNativeBinding().DetachState(secondState)
        || GetGuiLuaNativeBinding().IsStateInstalled(secondState)
        || GetGuiLuaNativeBinding().StateCount() != 1)
    {
        std::cerr << "Lua state detach failed\n";
        GetGuiLuaNativeBinding().DetachAll();
        closeState(secondState);
        closeState(state);
        FreeLibrary(module);
        return 16;
    }
    const GuiGameplayLifecycleSnapshot frontendLifecycle =
        service.GameplayLifecycle();
    if (frontendLifecycle.state
            != GuiGameplayLifecycleState::Frontend
        || !RunBooleanChunk(
            state,
            api,
            loadString,
            pcall,
            "local ok,status=ScriptedGuiNative.TryAcquireChannel("
            "'probe',10);return ok and status=='claimed'"
        ))
    {
        std::cerr << "Publisher detach lifecycle or takeover failed\n";
        GetGuiLuaNativeBinding().DetachAll();
        closeState(secondState);
        closeState(state);
        FreeLibrary(module);
        return 16;
    }

    if (!RunBooleanChunk(
            state,
            api,
            loadString,
            pcall,
            "package.path='script\\\\?.lua;'..package.path;"
            "local values={};"
            "CString=setmetatable({}, {__call=function(_,v)return v end});"
            "CFixedPoint=setmetatable({}, {__call=function(_,v)"
            "return {Get=function()return v end} end});"
            "CSetVariableCommand=function(tag,name,value)"
            "return {tag=tag,name=name,value=value:Get()} end;"
            "local variables={GetVariable=function(_,name)"
            "return CFixedPoint(values[name] or 0) end};"
            "local country={GetVariables=function()return variables end};"
            "local tag=setmetatable({GetCountry=function()return country end},"
            "{__tostring=function()return 'CHI' end});"
            "local ai={Post=function(_,command)"
            "values[command.name]=command.value end};"
            "local minister={GetCountryTag=function()return tag end,"
            "GetOwnerAI=function()return ai end};"
            "CCurrentGameState={GetAIRand=function()return 12345 end};"
            "local context={minister=minister,ministerTagObject=tag,"
            "ministerAI=ai,playerTag='CHI',currentDay=12};"
            "ProbePersistenceValues=values;"
            "ProbePersistenceContext=context;"
            "ProbePersistenceTag=tag;"
            "local P=require('scripted_gui_persistence');"
            "local profile,token,created,written="
            "P.EnsureProfileKey(context,'probe');"
            "local wrote=P.WriteNumber(context,'probe','counter',42.5);"
            "local value,available=P.ReadNumber(context,'probe','counter',0);"
            "local readProfile,profileAvailable,readToken="
            "P.ReadProfileKey(context,'probe');"
            "return profile:match('^CHI:%d+$')~=nil and token>0 "
            "and created and written and wrote and available "
            "and value==42.5 and profileAvailable "
            "and readProfile==profile and readToken==token"
        )
        || !RunBooleanChunk(
            state,
            api,
            loadString,
            pcall,
            "local chunk=loadfile('script/war_map_adapter.lua');"
            "return type(chunk)=='function'"
        )
        || !RunBooleanChunk(
            state,
            api,
            loadString,
            pcall,
            "package.loaded.war_map_adapter=nil;"
            "package.loaded.overlay_gui={"
            "DisplayRegionNames={'probe_region'},"
            "Tick=function()return {playerTag='CHI',regions={}} end};"
            "package.loaded.gui_action_bridge=nil;"
            "local actions=require('gui_action_bridge');"
            "local pendingAction='open_china_anti_jap_warmap';"
            "package.loaded.gui_data_bridge={"
            "DispatchActions=function()"
            "if not pendingAction then return 0 end;"
            "local action=pendingAction;pendingAction=nil;"
            "return actions.Dispatch(action,{action=action}) and 1 or 0 end,"
            "PublishSnapshot=function()return true end};"
            "local fallbackValues={};"
            "local fallbackVariables={GetVariable=function(_,name)"
            "return CFixedPoint(fallbackValues[name] or 0) end};"
            "local fallbackCountry={GetVariables=function()"
            "return fallbackVariables end};"
            "local fallbackTag=setmetatable({GetCountry=function()"
            "return fallbackCountry end},"
            "{__tostring=function()return 'JAP' end});"
            "local fallbackAi={Post=function(_,command)"
            "local target=tostring(command.tag);"
            "local store=target=='CHI' and ProbePersistenceValues "
            "or fallbackValues;store[command.name]=command.value end};"
            "local fallbackMinister={GetCountryTag=function()"
            "return fallbackTag end,GetOwnerAI=function()"
            "return fallbackAi end};"
            "CCountryDataBase={GetTag=function(name)"
            "return name=='CHI' and ProbePersistenceTag or fallbackTag end};"
            "local context={minister=fallbackMinister,"
            "ministerTagObject=fallbackTag,ministerAI=fallbackAi,"
            "ministerTag='JAP',playerTagObject=ProbePersistenceTag,"
            "playerTag='CHI',currentDay=12};"
            "context.publisherStateOrdinal=9;"
            "context.requestRefresh=function()"
            "ProbePersistenceRefreshRequested=true end;"
            "local P=require('war_map_adapter');"
            "P.OnPublisherAcquired(context);"
            "if not P.RestoreState(context) "
            "or P.SessionId~=P.PersistenceKey then "
            "return false end;"
            "local initialSession=P.SessionId;"
            "if P.PumpActions(context,64)~=1 or not P.WindowOpen "
            "or ProbePersistenceValues.sgui_china_war_revision~=1 "
            "or fallbackValues.sgui_china_war_revision~=nil then "
            "return false end;"
            "P.PumpActions(context,64);"
            "if P.PersistenceRevision~=1 "
            "or P.PendingPersistenceRevision~=nil then return false end;"
			"ProbePersistenceValues.sgui_china_war_revision=0;"
            "P.PumpActions(context,64);"
            "local result=P.WindowOpen==false and P.PersistenceRevision==0 "
            "and P.SessionId==initialSession..':reload:1' "
            "and ProbePersistenceRefreshRequested==true;"
            "package.loaded.war_map_adapter=nil;"
            "package.loaded.overlay_gui=nil;"
            "package.loaded.gui_action_bridge=nil;"
            "package.loaded.gui_data_bridge=nil;"
            "return result"
        ))
    {
        std::cerr << "Lua persistence adapter probe failed\n";
        GetGuiLuaNativeBinding().DetachAll();
        closeState(secondState);
        closeState(state);
        FreeLibrary(module);
        return 17;
    }

    std::unique_ptr<IGuiDataBridgeChannel> runtimeChannel =
        service.CreateChannel("runtime_probe", {});
    if (!runtimeChannel
        || !runtimeChannel->Open(
            GuiDataProviderInitContext{root},
            error
        )
        || !RunBooleanChunk(
            state,
            api,
            loadString,
            pcall,
            "package.path='script\\\\?.lua;'..package.path;"
            "Utils={LUA_DEBUGOUT=function() end};"
            "CCurrentGameState={"
            "GetPlayer=function() return 'CHI' end,"
            "GetCurrentDate=function() return {"
            "GetTotalDays=function() return 12 end} end};"
            "package.preload.runtime_probe_plugin=function() "
            "local M={pumps=0,builds=0};"
            "function M.PumpActions() M.pumps=M.pumps+1;return true end;"
            "function M.ShouldRefresh() return M.builds==0 end;"
            "function M.BuildUpdate(context) "
            "M.builds=M.builds+1;return {version=2,revision=M.builds,"
            "fullSnapshot=true,values={['state.visible']=true,"
            "['state.viewertag']='CHI'},"
            "lists={}} end;RuntimeProbePlugin=M;return M end;"
            "package.preload.runtime_bad_plugin=function() "
            "local M={ticks=0};function M.Tick() "
            "M.ticks=M.ticks+1;error('isolated_failure') end;"
            "RuntimeBadPlugin=M;return M end;"
            "package.loaded.scripted_gui_plugins={plugins={"
            "{id='runtime_bad',channel='runtime_bad',"
            "module='runtime_bad_plugin',scope='player_only',"
            "priority=10,maxConsecutiveErrors=1},"
            "{id='runtime_probe',channel='runtime_probe',"
            "module='runtime_probe_plugin',scope='player_only'}}};"
            "local R=require('scripted_gui_runtime');"
            "local minister={GetCountryTag=function() return 'CHI' end};"
            "return R.Tick(minister) and RuntimeProbePlugin.pumps==1 "
            "and RuntimeProbePlugin.builds==1 "
            "and RuntimeBadPlugin.ticks==1 "
            "and R.GetDiagnostics().runtime_bad~=nil"
        ))
    {
        std::cerr << "Generic Lua scheduler isolation failed: "
                  << error << '\n';
        if (runtimeChannel)
        {
            runtimeChannel->Close();
        }
        GetGuiLuaNativeBinding().DetachAll();
        closeState(secondState);
        closeState(state);
        FreeLibrary(module);
        return 17;
    }
    GuiDataBridgeUpdate runtimeUpdate;
    if (runtimeChannel->Poll(runtimeUpdate, error)
            != GuiDataBridgePollResult::Update
        || !runtimeUpdate.fullSnapshot
        || runtimeUpdate.values.find("state.visible")
            == runtimeUpdate.values.end())
    {
        std::cerr << "Generic Lua scheduler did not publish data\n";
        runtimeChannel->Close();
        GetGuiLuaNativeBinding().DetachAll();
        closeState(secondState);
        closeState(state);
        FreeLibrary(module);
        return 18;
    }
    RunBooleanChunk(
        state,
        api,
        loadString,
        pcall,
        "return require('scripted_gui_runtime').Shutdown()"
    );

    runtimeChannel->Close();
    channel->Close();
    nativeEffects.UnregisterHandler("probe.add");
    nativeEffects.SetGameplayContext(false, {}, 0);
    nativeQueries.UnregisterProvider("probe");
    nativeQueries.SetGameplayContext(false, {}, 0);
    nativeQueries.Configure(nullptr, nullptr);
    GetGuiLuaNativeBinding().SetReverseProbeRunner({});
    GetGuiLuaNativeBinding().DetachAll();
    closeState(secondState);
    closeState(state);
    FreeLibrary(module);
    std::cout << "Lua 5.1 native bridge probe passed\n";
    return 0;
}
