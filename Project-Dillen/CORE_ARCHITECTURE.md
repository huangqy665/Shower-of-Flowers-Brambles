# New Core architecture

`new_core` is the shared in-process extension runtime for HOI3. Native Effects,
Script GUI, and Leader Capture are registered modules, not owners of the
process bootstrap, hooks, or game lifecycle.

## Core infrastructure

### Module registry

Files:

- `src/core/core_module.h`
- `src/core/core_module_registry.h/.cpp`

Every injected mechanism implements `core::IModule` and provides a stable ID,
startup priority, initialization, lifecycle handling, ticking, and shutdown.
Modules are initialized in ascending priority order and shut down in reverse
order. Duplicate IDs are rejected. Exceptions in lifecycle and tick callbacks
are isolated so one module cannot stop dispatch to the remaining modules.

Modules register before the first hook installation. This keeps startup
deterministic and prevents runtime code patches from appearing before all
modules have completed configuration validation.

```cpp
class ExampleModule final : public core::IModule
{
public:
    std::string_view Id() const override { return "example"; }
    int Priority() const override { return 200; }

    bool Initialize(
        core::Services& services,
        std::string& error
    ) override;

    void OnLifecycleEvent(
        const core::LifecycleEvent& event
    ) override;

    void Tick(uint64_t nowMilliseconds) override;
    void Shutdown() override;
};
```

### Hook registry

Files:

- `src/core/core_hook_registry.h/.cpp`

Each module registers one or more `core::HookDefinition` objects. A definition
contains a unique ID, priority, install/uninstall/status callbacks, and an
optional maintenance callback. Installation follows priority order;
uninstallation uses reverse order. Successfully installed hooks remain active
when another hook is temporarily unavailable, allowing the startup worker to
retry without repeatedly patching working hooks.

The Script GUI module currently registers:

- `windows.d3d9`
- `windows.lua51`

The Leader Capture module registers one transactional patch group:

- `hoi3.leader_capture`

The D3D9 maintenance path is invoked through the generic core pump. Lua no
longer directly knows how to repair D3D9 hooks.

### Reverse probe framework

Files:

- `src/core/reverse_probe_framework.h/.cpp`
- `tests/reverse_probe_framework_probe.cpp`

`core::ReverseProbeFramework` is the only supported execution boundary for new
reverse-engineering probes. Every probe declares a stable ID, category, access
class, optional engine version, required symbols/types/fields/capabilities,
gameplay and stable-barrier requirements, and an evidence level. The framework
rejects duplicate IDs, isolates callback exceptions, supports single/selected/
all execution, and can append machine-readable JSONL reports carrying lifecycle,
barrier, player, duration, version, status, and evidence metadata.

The default policy permits metadata and memory reads only. Reversible patches
and writes require an explicit policy opt-in, active gameplay, and a live
`NativeSaveLoadBarrier` whose write gate is open. A caller-provided boolean is
not sufficient to bypass this gate. Startup currently runs a profile-structure
audit and validates every symbol that has a signature or expected CALL target;
a failed symbol is invalidated individually rather than disabling unrelated
engine capabilities.

Read probes may also require a stable barrier. The framework then acquires one
shared stable lease and passes it through the complete callback, allowing a
probe to combine several Native Queries and stable-object resolutions without
opening a nested barrier lock or crossing a SaveLoaded generation.

`hoi3_native_queries_module.cpp` registers two live read probes:
`hoi3.query.same_generation` validates a six-operation Native Query snapshot,
and `hoi3.objects.unit_leader_generation` validates the country Unit list,
active/reserve Leader registries, sampled object queries, and stable-ID
re-resolution after a later save generation. These replace the former external
Unit/Leader process-memory experiment. Lua invokes registered probes through
`NewCoreNative.RunReverseProbes(ids)`. The generic
`script/reverse_probe_runtime.lua` adapter consumes
`new_core/reverse_probe_runtime.request`, archives it as `.completed`, writes a
human-readable log, and the framework appends authoritative results to
`new_core/reverse_probe_runtime.jsonl`.

### Lifecycle service

Files:

- `src/core/core_lifecycle.h/.cpp`
- `src/hoi3/hoi3_lifecycle.h/.cpp`

`core::LifecycleService` publishes immutable before/after snapshots. Current
phases are `Unknown`, `Frontend`, and `Gameplay`. A transition records whether
gameplay was entered or exited and whether the player country changed.

The lifecycle snapshot also publishes `nativeWritesAllowed`, barrier generation,
and barrier reason. Any barrier transition increments the general lifecycle
generation, invalidating session `ObjectHandle` values before modules tick.

`src/native/native_save_load_barrier.h/.cpp` implements the fail-closed native
save-load barrier. Initial gameplay must produce three stable native samples
before writes open. An unavailable native state, changed GameState/world
fingerprint, player change, or date rewind closes writes immediately. After a
suspected load, three stable samples reopen writes and publish `SaveLoaded`.
`src/native/native_save_load_core_module.h/.cpp` hooks the validated save-file
deserialization CALL at `save_load.file_deserialize_call_site`. It invokes
`NotifySaveLoadStarted` before deserialization and `NotifySaveLoaded` after the
native call returns. Writes remain closed until three subsequent lifecycle
samples agree, so the completion edge cannot reopen mutation access early.
The hook was verified both on initial save entry and on a same-player-tag
reload. `NEW_CORE_DISABLE_NATIVE_SAVE_LOAD=1` is an emergency opt-out; the
observer remains a fail-closed fallback when the hook is unavailable.

The native HOI3 player-tag probe is polled by `core::Runtime`, not by the D3D9
GUI host. Consequently non-GUI modules can subscribe to the same frontend and
gameplay boundaries.

### Native effect bridge

Files:

- `src/native/native_effect_bridge.h/.cpp`
- `src/native/native_effect_core_module.h/.cpp`
- `script/native_effect_bridge.lua`

`core::NativeEffectService` is the synchronous game-effect boundary shared by
all injected modules. Lua calls `NewCoreNative.ExecuteEffects` from the active
HOI3 Lua thread; the bridge validates and applies the complete batch before the
call returns. It does not route through decisions, events, a render callback,
or a deferred GUI queue.

Effect implementations are registered by operation name through
`core::Services::effects`. A Handler prepares an apply closure and an optional
rollback closure. Atomic batches with more than one effect require rollback
support from every Handler. Preparation completes for the whole batch before
the first mutation, and an apply failure rolls back already-applied effects in
reverse order.

Execution is rejected outside gameplay or while the native save-load barrier
is closed. Before prepare, the effect service acquires a barrier write lease
and holds it through apply/rollback, so a concurrent load transition cannot
cross the transaction after a successful check. This is independent of
lifecycle-event gating. The first successful call in a
lifecycle generation binds execution to its HOI3 simulation thread; calls from
another thread are rejected. Entering a new game, loading a save, changing
player, or returning to the frontend resets that binding.

The bridge itself contains no HOI3 business fields or fixed engine addresses.
Modules that reverse engineer an engine operation register reusable names such
as `country.change_manpower` or `province.add_modifier`; Script GUI files and
Lua adapters consume those names without adding GUI-specific C++.

### Capability registry

Files:

- `src/core/capability_registry.h/.cpp`
- `tests/capability_registry_probe.cpp`

`core::CapabilityRegistry` is the process-wide support catalog. It imports the
active Engine Profile as `engine.symbol.*`, `engine.type.*`, and
`engine.field.*` entries, and receives `resolver.*`, `query.*`, and `effect.*`
entries from the services that own those operations. A capability snapshot
reports provider, kind, access class, active availability, invalidation reason,
rollback class, persistence class, multiplayer class, engine version, and
required symbols/types/fields.

Availability is evaluated when queried. An invalidated symbol, missing field,
inactive Engine Profile, or version mismatch therefore disables only the
dependent capability. Lua can inspect a single entry with
`NewCoreNative.GetCapability(id)` instead of assuming that a registered name is
safe on the current executable. Native Query also re-evaluates its capability
before dispatch, so capability invalidation is an execution gate rather than
advisory metadata.

### Stable object resolver

Files:

- `src/native/native_object_resolver.h/.cpp`
- `src/hoi3/hoi3_native_object_keys.h/.cpp`
- `tests/native_object_resolver_probe.cpp`
- `tests/hoi3_native_object_keys_probe.cpp`

`core::NativeObjectResolverService` maps
`{TypeId, stableId, optional stableName}` to a fresh
`engine::ObjectHandle` in the current lifecycle generation. It never accepts a
Lua-provided address. Public resolution acquires a stable SaveLoaded barrier
lease; query handlers that already hold a lease use `ResolveGuarded` to avoid a
nested lock. The returned handle is rejected if the lifecycle generation
changes during resolution. `ObjectHandle` preserves both stable key components,
so `Refresh` can re-resolve name-keyed definitions after a save load.

The HOI3 provider now registers nine resolvers: GameState and CountryDatabase
singletons, Country by canonical three-byte tag, Province by province ID,
TechnologyDefinition by normalized definition name, TechnologyStatus by country
tag, Relation by a pair of country tags, Unit by its two native 32-bit IDs, and
Leader by its two native object-ID components. Country, relation, Unit, and
Leader key packing are separate tested contracts.

Unit resolution starts at the global country database and scans each country's
intrusive Unit list. The TFH 4.02 profile records the list head, tail, and count
at `Country+0xBAC/+0xBB0/+0xBB4`, plus the Unit-list node layout. This is the
same root returned by the engine's `GetUnitsIterator` binding, so leaderless
Units are no longer excluded. Leader resolution scans both the active and
reserve country registries at `Country+0xE00` and `Country+0xE10`. The stable
key is the native object-ID pair stored at `Leader+0x08/+0x0C`. The generic
object constructor writes both components, the engine's duplicate check compares
both, and the native save writer serializes the second component as the
`active_leaders` key inside Leader context. An owning tag may constrain the
scan, but no pointer, list node, registry ordinal, historical definition ID, or
assigned Unit is part of the resolver identity.

### Native query service

Files:

- `src/native/native_query_service.h/.cpp`
- `src/native/native_access_core_module.h/.cpp`
- `src/hoi3/hoi3_native_queries_module.h/.cpp`
- `tests/native_query_service_probe.cpp`

`core::NativeQueryService` is the synchronous read-side counterpart to Native
Effects. Providers register a normalized operation, symbol/type/field
dependencies, multiplayer classification, and a handler. Execution requires
active gameplay, the bound HOI3 Lua/simulation thread, an unchanged lifecycle
generation, and a stable SaveLoaded barrier lease. Results use a recursive
null/bool/integer/number/string/list/object value model.

`ExecuteSnapshot`/`ExecuteSnapshotGuarded` execute up to 256 uniquely keyed
requests while holding one execution mutex, one immutable gameplay/lifecycle
context, and one stable barrier lease. A snapshot has a monotonic ID, caller
state ID, player tag, and lifecycle generation. If the generation or player
changes before completion, no partial values are exposed. The snapshot is a
short-lived value copy only; it never caches native addresses across
SaveLoaded.

The HOI3 provider exposes eighteen operations: ten country fixed-point
values (`manpower`, `diplomatic_influence`, `total_leadership`, `officer_pool`,
`convoys`, `escorts`, `free_spies`, `dissent`, `national_unity`, `neutrality`),
plus `country.identity`, `country.capital`, `province.status`, and
`game.current_date.total_days`, plus `technology.status`,
`diplomacy.relation`, `unit.status`, and `leader.status`. Country queries default
to the current player tag but accept a stable `tag`; province queries require
`province_id`; relation queries require `target_tag`; technology queries require
`technology`; `unit.status` requires `unit_id0` and `unit_id1`, while
`leader.status` requires `leader_id0` and `leader_id1`. Both optionally accept
an owning `tag`.

Lua uses:

```lua
local ok, value, code, message = NewCoreNative.Query(
    "country.manpower",
    { tag = "CHI" }
)
local registered = NewCoreNative.HasQuery("country.manpower")
local capability = NewCoreNative.GetCapability("query.country.manpower")

local batchOk, snapshot, batchCode, batchMessage =
    NewCoreNative.QuerySnapshot({
        {
            key = "manpower",
            operation = "country.manpower",
            arguments = { tag = "CHI" }
        },
        {
            key = "leadership",
            operation = "country.total_leadership",
            arguments = { tag = "CHI" }
        }
    })

if batchOk then
    local manpower = snapshot.values.manpower
    local leadership = snapshot.values.leadership
end
```

The HOI3 Native Effect provider now resolves Country, Province,
TechnologyDefinition, and Relation through the same service. Native Effect
execution passes its already-held SaveLoaded safety lease into those guarded
resolutions, so Effect preparation does not acquire a nested barrier lock. The
old parallel country-table, province-vector, technology-lookup, and relation-
table parsing paths have been removed from `hoi3_gameplay_effects.cpp`.

## Build graph and source layout

The production source tree mirrors the runtime boundaries instead of keeping a
flat list of implementation files:

| Directory | CMake component | Responsibility |
|---|---|---|
| `src/core` | `new_core_core`, `new_core_capabilities`, `new_core_reverse`, `new_core_runtime`, `new_core_handshake` | lifecycle, Hook/module registries, capabilities, probes, orchestration, handshake |
| `src/engine` | `new_core_engine` | versioned Symbol/Type/Profile registry and HOI3 TFH 4.02 schema |
| `src/native` | `new_core_native` | save-load barrier, stable resolver, queries, effects, native core modules |
| `src/hoi3` | `new_core_hoi3` | HOI3-specific lifecycle, object keys, query providers and effect handlers |
| `src/gui/model` | `new_core_gui_model` | parser, data values, conditions, actions and platform-neutral runtime model |
| `src/gui/data` | `new_core_gui_data` | provider and bridge implementations |
| `src/gui/runtime` | `new_core_gui_runtime` | plugins, sessions, windows, persistence, localization and render queue |
| `src/gui/lua` | `new_core_gui_lua` | Lua channels, native binding, state Hook and in-process application host |
| `src/gui/d3d9` | `new_core_gui_d3d9` | Windows input, D3D9 rendering, textures, text, maps and markers |
| `src/gui/module` | `new_core_script_gui` | `core::IModule` adapter for the complete Script GUI subsystem |
| `src/leader_capture` | `new_core_leader_capture` | battle-leader capture module and engine |
| `src/launcher` | `new_core_launcher_support` plus executables | launcher core, GUI launcher and development injector |
| `src/dll` | `scripted_gui_overlay` | stable DLL ABI and thin process entry |
| `src/tools` | standalone tools | offline indexed-map generation |
| `tests` | probe executables | CTest-only probes, absent when `BUILD_TESTING=OFF` |

The root `CMakeLists.txt` owns only project policy, x86 validation, generated mod
configuration, and subdirectory dispatch. `cmake/NewCoreTargets.cmake` applies
C++17, disabled compiler extensions, target-scoped MSVC options and shared target
defaults. Production code is compiled once into static components and reused by
the DLL and probes; tests no longer rebuild the same implementation sources in
each executable. Configuration fails unless the generator is Windows 32-bit
x86. `gui_lua51_native_probe` is a registered CTest and returns the standard
skip code when the configured Lua DLL is unavailable.

## Runtime and modules

Files:

- `src/core/core_runtime.h/.cpp`
- `src/engine/engine_registry.h/.cpp`
- `src/engine/engine_schema_hoi3_tfh_402.inc`
- `src/engine/engine_profile_hoi3_tfh_402.cpp`
- `src/engine/engine_abi_hoi3_tfh_402.h`
- `src/native/native_effect_core_module.h/.cpp`
- `src/native/native_access_core_module.h/.cpp`
- `src/hoi3/hoi3_native_queries_module.h/.cpp`
- `src/gui/module/script_gui_core_module.h/.cpp`
- `src/leader_capture/leader_capture_core_module.h/.cpp`
- `src/dll/scripted_gui_overlay_dll.cpp`

`core::Runtime` owns the hook registry, lifecycle service, and module registry,
and publishes the process-wide Engine Registry through `core::Services`.
It serializes exported calls, pumps hook maintenance, polls lifecycle state,
dispatches queued lifecycle events, and ticks modules.

The Engine Registry selects a named executable profile, resolves semantic
symbols, validates signatures and expected call targets, exposes versioned
native type layouts, and invalidates transient object handles by lifecycle
generation. The HOI3 lifecycle probe, gameplay effects, leader capture engine,
and D3D9 frame probes consume this registry instead of owning executable RVAs,
field offsets, signatures, or version timestamps.

`ScriptGuiCoreModule` owns the D3D9 host and adapts core lifecycle events to the
existing Lua GUI bridge. Existing `ScriptedGui_*` exports and the current DLL
filename remain unchanged for injector compatibility; the DLL entry is now a
thin adapter around `core::Runtime`.

`LeaderCaptureCoreModule` is the second module. Its capture and transfer engine
is implemented by `src/leader_capture/leader_capture_engine.h/.cpp`. It has no private
`DllMain`, injector, or worker thread. Hook installation belongs to
`HookRegistry`, its 100 ms controller polling belongs to the shared module Tick,
and gameplay exit, player changes, and save-load notifications clear all native
session pointers. Its native hook callbacks and Tick acquire the same save-load
barrier lease used by Native Effects, so this direct engine-write path cannot
bypass the shared safety boundary.

The former standalone project, launcher, build outputs, and compatibility DLL
have been removed. New reverse-engineered symbols and layouts must be added to
the version schema/profile before a module consumes them.

## Product launcher and handshake

Files:

- `src/launcher/new_core_launcher.cpp`
- `src/launcher/new_core_launcher_core.h/.cpp`
- `src/core/new_core_handshake.h/.cpp`
- `tests/new_core_launcher_probe.cpp`

`hoi3_new_core_launcher.exe` provides original-launcher and injected-game
modes. Its launch core validates PE32 i386 compatibility, creates HOI3,
injects `hoi3_new_core.dll`, and waits for a versioned shared-memory handshake.
The DLL reports runtime initialization, module IDs, Hook installation status,
and terminal readiness or failure. The launcher contains no gameplay-module or
GUI-plugin business logic.

The stable generic exports are `NewCore_GetAbiVersion`,
`NewCore_GetModuleIds`, `NewCore_GetHookStatuses`, and
`NewCore_GetLastError`. Existing `ScriptedGui_*` exports remain available for
the Script GUI module and compatibility tooling.
