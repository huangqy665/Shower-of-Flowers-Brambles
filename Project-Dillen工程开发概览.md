# 1. New Core 当前实现的注入机制

> 最后按仓库源码核对：2026-08-24。本文是 New Core 的统一工程概览、Script GUI 语法手册和 HOI3 逆向进度记录。实现状态以 `new_core/src`、`new_core/CMakeLists.txt`、`script`、`script_gui`、`interface` 中的当前文件为准；历史讨论、旧日志和 IDE 中已经删除的标签页不作为源码事实。

## 1.1 产品定位

New Core 已从单一 Script GUI Overlay 演变为 **HOI3 32 位进程内扩展核心**。它负责统一启动、DLL 注入、模块注册、Hook 管理、游戏生命周期、Lua 桥、D3D9 绘制和原生游戏数据修改。新增机制原则上实现为 `core::IModule`，而不是再创建互不协调的独立 DLL、注入器或后台线程。

当前目标程序固定为已经验证的 32 位 `hoi3_tfh.exe`：

| 项目 | 当前值 |
|---|---|
| PE 架构 | PE32 / i386 |
| PE 时间戳 | `0x50978B2F` |
| 映像大小 | `0x018B0000` |
| 已验证版本族 | HOI3 TFH 4.02 当前仓库使用版本 |
| 将领捕获内部版本标识 | `R06B-D328` |

当前绝大多数原生 RVA、对象偏移和函数调用约定仅对该可执行文件成立。版本不匹配时，通用原生效果模块拒绝执行；将领捕获模块还会逐项验证调用点、函数签名和补丁目标，验证失败时不安装任何补丁。

## 1.2 统一启动与注入

产品入口为 `hoi3_new_core_launcher.exe`，实现位于：

- `new_core/src/launcher/new_core_launcher.cpp`：Win32 启动器界面；
- `new_core/src/launcher/new_core_launcher_core.h/.cpp`：路径校验、进程创建、远程 `LoadLibraryW`、配置保存和握手；
- `new_core/src/core/new_core_handshake.h/.cpp`：版本化共享内存握手；
- `new_core/src/dll/scripted_gui_overlay_dll.cpp`：注入 DLL 的薄入口；
- `new_core/src/scripted_gui_overlay.def`：稳定导出 ABI。

启动器有两种模式：

1. **注入模式**：以挂起状态创建 `hoi3_tfh.exe`，注入 `hoi3_new_core.dll`，恢复主线程并等待 DLL 报告 `dll_worker_started -> runtime_initialized -> hooks_installing -> ready/failed`。
2. **原版模式**：只启动配置的 Paradox 原版启动器，不加载 New Core。

注入成功不能只以远程 `LoadLibraryW` 返回非零判断。最终握手还必须返回 ABI 版本、已注册模块 ID、Hook 状态和最终 readiness。启动器同时检查游戏与 DLL 均为 PE32 i386，并默认阻止重复启动 `hoi3_tfh.exe`。

## 1.3 核心基础设施

### Version / Symbol / Type Registry

`engine_registry.h/.cpp` 已形成统一、只读、可查询和可失效的引擎接口层；`engine_schema_hoi3_tfh_402.inc` 是当前受支持版本全部已知符号和字段布局的单一事实源，`engine_profile_hoi3_tfh_402.cpp` 负责组装 HOI3 TFH 4.02 D328 版本配置及签名/调用目标验证规则，`engine_abi_hoi3_tfh_402.h` 集中保存必须在编译期成立的 32 位原生命令与 Effect ABI。

- Version Registry 统一识别 PE machine、时间戳、映像大小和可选 checksum；版本不匹配时整个原生接口层保持 inactive。
- Symbol Registry 记录语义名称、RVA、符号类别、调用约定、可信等级、字节签名和预期 CALL 目标；支持按枚举或字符串查询、运行时解析、逐符号验证和逐符号失效。
- Type Registry 记录 GameState、Country、Province、Unit、Leader、Combat、研究/建筑节点和原生命令对象的类型、字段值、字段大小、布局值类别、读写权限、语义与生命周期；已知原生 ABI 类型还记录实际 `sizeof`。
- `ObjectHandle` 使用类型、数值稳定 ID、可选字符串稳定名、地址和 lifecycle generation 标记临时原生对象；生命周期代际变化后旧 Handle 会统一失效。
- 热路径的 RVA/字段查询使用不可变 Profile 加原子状态，不经过全局互斥锁；字符串诊断与版本切换仍受锁保护。

当前 `hoi3_lifecycle.cpp`、`hoi3_gameplay_effects.cpp`、`leader_capture_engine.cpp` 和 `gui_d3d9_hook.cpp` 已迁移为 Registry 消费者。新的逆向结果不得再以裸 RVA、对象偏移、版本时间戳或签名字节写入业务 `.cpp`，而应先注册到版本 Schema/Profile，再由业务代码按语义 ID 查询。

### Capability Registry / Stable Object Resolver / Native Query Service

`capability_registry.h/.cpp`、`native_object_resolver.h/.cpp` 和 `native_query_service.h/.cpp` 已形成第一版统一读侧接口层：

- Capability Registry 自动导入活动 Profile 的 `engine.symbol.*`、`engine.type.*`、`engine.field.*`，并汇总 `resolver.*`、`query.*`、`effect.*`；查询结果包含提供者、读写类别、当前可用性、失效原因、回滚、持久化、多人等级、版本与符号/类型/字段依赖。Native Query 在分发前会再次求值能力状态，失效能力不仅用于显示，还会直接拒绝执行。
- Stable Object Resolver 只接受 `{TypeId, stableId, optional stableName}`，每次在当前 lifecycle generation 内重新解析地址并返回 `ObjectHandle`；不允许 Lua 或配置传入裸指针。公开解析、Query 与 Effect 事务均持有 Native SaveLoaded 稳定租约，解析期间代际变化会使结果失败。
- `hoi3_native_object_keys.h/.cpp` 独立定义国家 Tag、双国关系、Unit/Leader 原生双 ID 和定义名规范化规则；当前已注册 GameState、CountryDatabase、Country、Province、TechnologyDefinition、TechnologyStatus、Relation、Unit 和 Leader 共 9 类 Resolver。Unit 从国家单位链按原生双 ID 重解析，Leader 从现役/预备将领表按原生对象 ID 对重解析。
- Native Query Service 是 `NativeEffectService` 的只读对称层，统一执行 Gameplay、HOI3 Lua/模拟线程、生命周期代际和 SaveLoaded 屏障校验，并使用递归值模型返回标量、列表或对象。`ExecuteSnapshot`/`ExecuteSnapshotGuarded` 可在同一个执行锁、不可变生命周期上下文和稳定屏障租约内批量执行最多 256 个唯一键查询；快照携带单调 ID、玩家 Tag、调用 State 与 generation，代际或玩家在结束前变化时不暴露部分结果，也不缓存跨 SaveLoaded 的原生地址。
- Lua 已开放 `NewCoreNative.Query(operation, arguments)`、`NewCoreNative.QuerySnapshot(requests)`、`NewCoreNative.HasQuery(operation)` 和 `NewCoreNative.GetCapability(id)`；脚本不需要知道版本地址、对象布局和解析过程。批量请求使用 `{ key, operation, arguments }`，结果通过 `snapshot.values[key]` 和 `snapshot.results[key]` 读取。

当前 HOI3 查询提供器注册 18 个操作：原有 14 个 Country/Province/GameState 查询，以及 `technology.status`、`diplomacy.relation`、`unit.status`、`leader.status`。国家查询默认使用当前玩家，也可显式传稳定 `tag`；省份查询使用 `province_id`；科技使用规范化定义名；关系使用源/目标 Tag；Unit 与 Leader 均使用各自的原生双 ID；二者均可用所属国 Tag 缩小解析范围。国家单位链覆盖无将领单位，现役与预备将领表覆盖已任命和未任命将领。

### ReverseProbeFramework

`reverse_probe_framework.h/.cpp` 已建立统一逆向探针执行边界。每个探针必须声明稳定 ID、类别、访问等级、可选版本、依赖 Symbol/Type/Field/Capability、Gameplay/稳定屏障要求与执行回调；结果记录生命周期代际、屏障代际、玩家、状态、证据等级、版本、耗时和诊断，并可追加为 JSONL。框架支持单项、指定集合和全部探针运行。默认策略只允许元数据与只读内存探针；可逆补丁和写内存探针必须显式放宽策略，同时满足 Gameplay 和真实 `NativeSaveLoadBarrier` 已开放，调用者不能仅伪造布尔值绕过安全门。只读探针也可要求稳定屏障，此时整个回调共享一份稳定租约。

运行时启动阶段已经执行两项核心探针：Profile/Symbol/Type/Field 结构一致性检查，以及对所有带签名或预期 CALL 目标符号的实机验证。HOI3 查询模块另注册 `hoi3.query.same_generation` 与 `hoi3.objects.unit_leader_generation` 两个按需只读实机探针：前者验证六项查询处于同一快照代际，后者扫描玩家国家 Unit 链和现役/预备 Leader 表、执行样本对象 Query，并在后续 saveGeneration 中按稳定双 ID 重解析旧样本。原先散落在外部 PowerShell 进程内存脚本中的 Unit/Leader 代际实验已迁入该框架。

Lua 通过 `NewCoreNative.RunReverseProbes(ids)` 在 HOI3 Lua/模拟线程触发已注册探针；`script/reverse_probe_runtime.lua` 读取 `new_core/reverse_probe_runtime.request`，完成后归档为 `.completed` 并写入 `reverse_probe_runtime.log`，框架将权威结果追加到 `new_core/reverse_probe_runtime.jsonl`。请求文件每行一个 Probe ID，写 `all` 可运行全部只读探针。验证失败只使对应能力或探针失败，不会无条件拖垮其他已验证能力。

2026-08-24 实机跨读档验收已经完成：首轮报告为 `save_generation=1`，重新读取同一存档后为 `save_generation=2`；两轮六项 QuerySnapshot 均保持同代际，旧 Unit、现役 Leader、预备役 Leader 三个稳定 ID 全部在新代际重解析成功，`previous_resolved=3`、`previous_address_changed=3`、`previous_missing=0`。这证明稳定键能够跨 SaveLoaded 重新定位对象，同时旧原生地址确实不会被错误复用。

### 模块注册器

`core_module_registry.h/.cpp` 按优先级初始化模块、分发生命周期、执行 Tick，并在关闭时逆序卸载。当前实际注册 7 个模块：

| 优先级 | 模块 ID | 实现 | 责任 |
|---:|---|---|---|
| -300 | `native_save_load` | `native_save_load_core_module.cpp` | 注册精确 SaveLoaded Hook，驱动原生读档屏障。 |
| 40 | `native_access` | `native_access_core_module.cpp` | 组装 Capability、Stable Object Resolver、Native Query，接入 Engine Registry、生命周期与 SaveLoaded 稳定租约。 |
| 50 | `native_effects` | `native_effect_core_module.cpp` | 拥有通用 `NativeEffectService` 的 Gameplay、玩家、生命周期代际和执行线程约束。 |
| 55 | `hoi3_native_queries` | `hoi3_native_queries_module.cpp` | 注册 HOI3 稳定对象解析器、18 个原生只读查询及同代际/Unit-Leader 实机探针。 |
| 60 | `hoi3_gameplay_effects` | `hoi3_gameplay_effects_module.cpp`、`hoi3_gameplay_effects.cpp` | 注册 59 个 HOI3 原生效果 Handler，并维护事件/决议延迟队列。 |
| 100 | `script_gui` | `script_gui_core_module.cpp` | 拥有 D3D9 宿主、Lua 5.1 桥、GUI 生命周期和兼容导出。 |
| 200 | `leader_capture` | `leader_capture_core_module.cpp`、`leader_capture_engine.cpp` | 安装战斗歼灭路径补丁，捕获并转移将领。 |

### Hook 注册器

`core_hook_registry.h/.cpp` 统一保存 Hook ID、优先级、安装、维护、状态检查和逆序卸载回调。当前真实注册的 Hook 有四组：

| 优先级 | Hook ID | 所属模块 | 作用 |
|---:|---|---|---|
| 100 | `windows.d3d9` | `script_gui` | 挂接 D3D9 创建、Reset、Present/EndScene、交换链 Present 和窗口消息输入。 |
| 200 | `windows.lua51` | `script_gui` | 捕获 HOI3 创建的 Lua 5.1 State，安装数据、动作和原生效果绑定。 |
| 300 | `hoi3.leader_capture` | `leader_capture` | 事务化安装 4 个 CALL 补丁和 1 个共享移除函数 Inline Hook。 |
| -1000 | `native_save_load.file_deserialize` | `native_save_load` | 挂接保存文件包装器中的反序列化 CALL，在同 Tag 读档开始时闭锁写入，并在原生调用返回后进入稳定恢复阶段。 |

原生数据操作注册在 `NativeEffectService`，不是 `HookRegistry` 中的机器码 Hook。二者必须区分：Hook 改变控制流；Native Effect Handler 在已经取得的 HOI3 Lua/模拟线程中调用原版函数或受控字段写入。

### 生命周期服务

`core_lifecycle.h/.cpp`、`hoi3_lifecycle.h/.cpp` 和 `native_save_load_barrier.h/.cpp` 发布 `Unknown`、`Frontend`、`Gameplay` 三种阶段，并记录玩家 Tag、generation、saveGeneration、原生写入许可、屏障代际与闭锁原因。

当前原生探针通过 `GameState` 单例、玩家 Tag、玩家国家对象、省份容器、国家表和当前日期形成生命周期采样。初次进入战局必须连续取得 3 次稳定采样才开放原生写入；原生状态不可读、世界指纹变化、玩家变化或日期回退会立即闭锁。疑似读档后再次稳定时，屏障先开放写入，再发布统一 `SaveLoaded`。以下安全门控已经接入：

- Script GUI 在屏障闭锁、退出战局、玩家变化和 SaveLoaded 时释放 Lua 频道所有权；
- Native Effect 同时执行生命周期门控和屏障写许可租约；租约从 prepare 前持有到 apply/rollback 完成，闭锁期间即使上下文误标为 Gameplay 也拒绝写入，读档闭锁也不能并发穿过已开始的事务；
- 注入层事件/决议队列在屏障闭锁、进入/退出战局、玩家变化、SaveLoaded 和 RuntimeStopping 时清空，并且闭锁期间不执行 Tick；
- Leader Capture 在屏障闭锁时立即停用 Hook 业务路径并清空临时对象、Held 队列和监视状态；其原生 Hook 回调、捕获写入和 Tick 同样持有屏障状态租约，不依赖一次性的 Gameplay 布尔检查；
- Engine Registry 在每次屏障/生命周期代际变化后使旧 Session `ObjectHandle` 失效。

保存文件包装器 `save_load.file_wrapper`、世界反序列化函数 `world_load.deserialize_core` 及其 CALL/返回点已经进入 Version/Symbol Registry，并通过初次读档与同玩家 Tag 二次读档实机验证。Hook 在原生反序列化开始前闭锁写入，在调用返回后只标记完成；显式读档的待确认状态会保留穿过引擎短暂的 Frontend 过渡，随后仍需连续 3 次稳定 Gameplay 生命周期采样才重新开放并递增 `saveGeneration`。`NEW_CORE_DISABLE_NATIVE_SAVE_LOAD=1` 可在紧急情况下禁用精确 Hook；观察式 fail-closed 检测继续作为回退路径。

## 1.4 Script GUI 注入机制

Script GUI 是 New Core 的第一个完整子系统。它通过 D3D9 Hook 在游戏进程内绘制，通过窗口消息 Hook 接收输入，通过 Lua 5.1 Hook 获取实时游戏数据并回传动作。其核心原则是：

1. `.sgui` 定义窗口、控件、坐标、层级、条件和事件名；
2. `.sgfx` 定义 Sprite、进度条资源、索引地图和内置 2D 效果；
3. `interface/gui_plugins/*.txt` 注册窗口、数据源、启动方式和刷新周期；
4. `script_gui/*.txt` 定义动作到 Lua 函数或离线回退操作的绑定；
5. `script_gui/data/*.txt` 保存静态目录、列表和业务参数；
6. `script/*.lua` 读取 HOI3 状态、发布数据快照并消费 GUI 动作；
7. C++ 解释器和宿主只实现通用控件、渲染、输入、数据和生命周期，不硬编码战争地图业务字段。

当前完整实例：

- `china_anti_jap`：索引地图、Region 着色与点击、Marker、主官任命、战争进度、动态列表、省份任务和原生效果事务；
- `parliament`：使用通用列表与极坐标/半圆布局构建的纯声明式议会席位图。

省份任务已经形成完整链路：任务静态数据位于 `script_gui/data/china_anti_jap_common.txt`，按钮和 Tooltip 位于 `interface/china_anti_jap.sgui`，行为绑定位于 `script_gui/china_anti_jap_warmap.txt`，Lua 在 `script/war_map_adapter.lua` 中把选中 Region 展开为 Province ID 列表，再以一个原子事务执行 `country.add_manpower` 与多个 `province.add_modifier`。C++ 中没有战争地图任务名、Region 名、消耗值或 Modifier 名。

## 1.5 通用原生效果机制

`NativeEffectService` 是 Lua 与 HOI3 原生写操作之间的统一边界：

- Lua 使用 `NewCoreNative.HasEffect(name)` 查询能力；
- Lua 使用 `NewCoreNative.ExecuteEffects(batch)` 同步提交批次；
- Handler 先解析稳定标识和参数，再生成 `apply` 与可选 `rollback`；
- 原子批次先完成全部 prepare，再执行写入；失败时逆序回滚；
- 非 Gameplay、Native SaveLoaded 屏障关闭、错误可执行文件、错误线程、缺失 Handler 或无回滚能力的多项原子批次会被拒绝；
- GUI Draw/Input 回调不直接写游戏数据，动作必须回到 HOI3 Lua/模拟线程执行。

当前 `hoi3_gameplay_effects.cpp` 注册 59 个操作，覆盖国家经济与政治、省份归属与建筑、科技与研究、Modifier、外交数值、间谍与情报、全局 Flag、事件/决议立即执行和注入层延迟队列。完整清单见本文第 4 节。

## 1.6 将领俘虏机制

`leader_capture` 只介入 **战斗导致的永久部队消失**，明确忽略手动解散路径。系统在战斗结果与两条已确认的战斗移除路径上建立归属上下文，先让原版完成单位/将领解绑，再从败方将领池移除将领并转移给已确认的俘获国。

当前归属规则：

- 胜方只有一个国家：直接作为俘获国；
- 多国联军：只有当战斗省份当前控制者属于胜方国家集合时，控制者才作为俘获国；
- 无法可靠归属：将领进入 Held 状态，不猜测国家，也不盲目转移。

详细源码分析、安全边界和已知限制见本文第 3 节。

## 1.7 当前构建与验收状态

- 根 `new_core/CMakeLists.txt` 已从 875 行压缩为 44 行；生产源码按 `core`、`engine`、`native`、`hoi3`、`gui`、`leader_capture`、`launcher`、`dll` 和 `tools` 分层，GUI 又拆分为 `model/data/runtime/lua/d3d9/module` 六个边界。各层通过静态组件 target 复用，不再在数十个 Probe 中反复编译同一实现文件。
- CMake 配置强制 Windows PE32/x86，统一使用 C++17 并显式关闭 compiler extensions；MSVC 参数通过 `new_core_project_options` 按 target 传播。`BUILD_TESTING=OFF` 时不会生成任何 Probe target。
- 当前登记 35 个 CTest Probe，`gui_lua51_native_probe` 已正式注册并使用可配置的 `NEW_CORE_LUA51_DLL`；Lua DLL 不存在时按标准跳过码退出。
- 2026-08-24 最新 Win32 Debug 全量回归为 35 项中 34 项通过、0 项失败，`gui_host_d3d9_probe` 因测试进程没有图形设备而按设计跳过；Lua 5.1 实桥探针通过。实机跨读档测试确认 `saveGeneration` 正确递增、单项 Query 与同代际 QuerySnapshot 正常、三个 Unit/Leader 稳定 ID 在原生地址全部改变后仍可重新解析。
- 战争地图已经在 HOI3 内完成绘制、输入穿透、窗口开关、动态占领数据、主官状态持久化和省份任务实机测试。
- 原生效果已完成分组可用性、读回、回滚及实机验证；全局 Flag、事件、决议、延迟执行、分类取消、通用取消和跨玩家存档队列清理有当前测试脚本与日志。
- `events/NewCoreNativeEffectProbe.txt`、`decisions/NewCoreNativeEffectProbe.txt`、`script/native_effect_live_probe.lua`、`script/reverse_probe_runtime.lua` 及其 `new_core/*.request/*.log/*.jsonl` 是开发探针资产，不是最终玩法内容。

# 2. Script GUI 系统简介与 SGUI/SGFX 通用语法手册

**本节以当前仓库源码为准，覆盖：**

- Script GUI 解释器的核心源码分类与职责；
- 当前核心能力的完成度和验收条件；
- `.sgui`、`.sgfx` 已实现的全部控件、资源和字段；
- 当前 `.sgui`、`.sgfx` 尚未使用、但解释器已经实现的语句；
- 已被解析但暂时没有运行时效果，以及当前配置中存在但会被忽略的语句。

## 1. 状态标记

| 标记 | 含义 |
|---|---|
| **现用** | 已在 `interface/*.sgui` 或 `interface/*.sgfx` 中实际使用，并有对应运行时实现。 |
| **已实现未用** | 当前 `.sgui/.sgfx` 未使用，但解析、布局、渲染或事件逻辑已经实现。 |
| **部分实现** | 仅对特定控件、特定平台或特定取值生效。 |
| **仅解析** | 字段会被读取并保存到 C++ 定义对象，但当前运行时不消费它。 |
| **忽略** | 通用语法解析器允许它存在，但解释器没有读取该字段，因此没有任何效果。 |

> 重要：解析器允许未知字段通过语法解析。一个字段“没有报错”不等于它“已经生效”。应以本文档和源码中的字段注册逻辑为准。

## 2. 核心源码分类

### 2.0 New Core 基础设施

- `new_core/src/core/core_module.h`、`core_module_registry.h/.cpp`：统一注册、排序、初始化、Tick、生命周期分发与逆序关闭所有注入模块；Script GUI 只是首个模块。
- `new_core/src/core/core_hook_registry.h/.cpp`：统一管理 Hook 的安装、状态、维护和逆序卸载，当前承载 D3D9、Lua 5.1 与将领捕获补丁组。
- `new_core/src/core/reverse_probe_framework.h/.cpp`：统一注册和执行版本化逆向探针，实施访问等级、依赖符号、Gameplay、SaveLoaded 屏障、异常隔离、证据等级和 JSONL 报告约束。
- `new_core/src/core/core_lifecycle.h/.cpp`、`new_core/src/hoi3/hoi3_lifecycle.h/.cpp`：统一发布主菜单、战局、玩家变化、世界指纹和日期采样。
- `new_core/src/native/native_save_load_barrier.h/.cpp`：实现 fail-closed 读档屏障、稳定采样恢复、显式原生 Hook 接口与统一写入许可。
- `new_core/src/core/core_runtime.h/.cpp`：拥有模块、Hook、生命周期、ReverseProbeFramework 和 Native SaveLoaded Barrier，是注入 DLL 的总运行时。
- `new_core/src/native/native_effect_bridge.h/.cpp`、`native_effect_core_module.h/.cpp`：通用原生效果事务、Handler 注册表、Gameplay/线程约束和 Lua 调用边界；本身不含 HOI3 地址或战争地图业务。
- `new_core/src/hoi3/hoi3_gameplay_effects.h/.cpp`、`hoi3_gameplay_effects_module.h/.cpp`：面向当前已验证 HOI3 可执行文件的 59 个原生效果 Handler，以及事件/决议延迟队列。
- `new_core/src/gui/module/script_gui_core_module.h/.cpp`：将原 Script GUI 宿主封装为 `core::IModule`，保留现有 `ScriptedGui_*` 导出 ABI。
- `new_core/src/leader_capture/leader_capture_core_module.h/.cpp`：第二个核心模块；通过统一 Hook 注册表接管将领捕获补丁，并通过统一 Tick 与游戏生命周期控制业务启停和原生指针清理。
- `new_core/src/leader_capture/leader_capture_engine.h/.cpp`：将领捕获、胜方归属判定、将领池移除/转移、补丁安装回滚及诊断状态的核心引擎；不存在独立 DLL 入口或私有 Worker。

### 2.1 配置解析与定义模型

- `new_core/src/gui/model/gui_interpreter.h/.cpp`：通用词法/语法解析器；注册 Sprite、进度条、内置效果和索引地图资源；构建窗口和控件树；解析相对坐标、条件、事件、列表模板、Z 顺序、裁剪与 2D 变换，并提供变换后命中和效果采样核心。
- `new_core/src/gui/runtime/gui_plugin_manifest.h/.cpp`：解析 `interface/gui_plugins` 中的插件清单，将窗口、数据源、启动方式、可见条件和窗口层级注册为可启动插件。
- `new_core/src/gui/model/gui_behavior.h/.cpp`：解析 `script_gui/*.txt` 行为文件，将 `.sgui` 中的动作名映射到 Lua 函数、触发阶段、条件、参数和离线回退操作。
- `new_core/src/gui/model/gui_declarative_data.h/.cpp`：解析静态或离线数据文件，生成标量与列表数据。
- `new_core/src/gui/runtime/gui_localization.h/.cpp`：读取本地化文本，并为 `localizationKey`、`localized` 和 Marker 提示提供翻译。

### 2.2 插件、应用与窗口生命周期

- `new_core/src/gui/runtime/gui_plugin.h`、`gui_plugin_registry.h/.cpp`：定义插件接口、插件工厂和插件描述符。
- `new_core/src/gui/runtime/gui_builtin_plugins.h/.cpp`：注册通用 `declarative_gui` 插件工厂。
- `new_core/src/gui/runtime/declarative_gui_plugin.h/.cpp`：通用声明式插件；把任意窗口与通用数据提供器连接起来，不包含战争地图专用业务逻辑。
- `new_core/src/gui/lua/gui_inprocess_application.h/.cpp`：加载所有界面、资源、行为和插件清单；执行基础配置校验；隔离配置错误；创建有效插件实例。
- `new_core/src/gui/runtime/gui_window_session.h/.cpp`：管理单个 GUI 会话的绑定、刷新、开关、列表实例、输入、动作、数据、临时状态和持久化状态。
- `new_core/src/gui/runtime/gui_window_manager.h/.cpp`：管理多窗口打开状态、可见状态、窗口 Z 顺序和模态窗口。
- `new_core/src/gui/runtime/gui_application_bus.h/.cpp`：执行跨窗口的打开、关闭、显隐和动作转发。
- `new_core/src/gui/runtime/gui_tick.h/.cpp`：按插件配置的刷新周期调度数据更新。

### 2.3 布局、列表、输入与事件

- `new_core/src/gui/model/gui_runtime.h/.cpp`：条件环境、通用事件路由、拖动参数、列表布局、滚动状态和命中测试。
- `new_core/src/gui/runtime/gui_render_queue.h/.cpp`：将控件树转换为统一渲染命令，并按全局 Z 顺序稳定排序。
- `new_core/src/gui/runtime/gui_custom_widget.h/.cpp`：为无法由内置控件表达的特殊控件提供 C++ 扩展注册点。
- `new_core/src/gui/model/gui_list_model.h`：通用动态列表和列表项数据结构。

### 2.4 数据驱动、Lua 桥与统一发布者

- `new_core/src/gui/model/gui_data.h/.cpp`：通用 `GuiDataRegistry`；保存布尔、整数、浮点、字符串和列表；解析数据路径、条件表达式和 `{变量}` 插值。
- `new_core/src/gui/data/gui_data_provider.h/.cpp`：通用数据提供器接口和注册表。
- `new_core/src/gui/data/gui_file_data_provider.h/.cpp`：从声明式数据文件加载快照。
- `new_core/src/gui/data/gui_sequence_data_provider.h/.cpp`：按顺序播放离线快照，主要用于原型和回退测试。
- `new_core/src/gui/data/gui_data_bridge.h/.cpp`：把外部发布者提供的数据快照导入统一数据注册表。
- `new_core/src/gui/lua/gui_lua_bridge.h/.cpp`：维护 Lua 数据频道、更新序号、动作队列、会话边界和游戏生命周期快照。
- `new_core/src/gui/lua/gui_lua_native_binding.h/.cpp`：在 HOI3 的 Lua 5.1 State 中注册原生数据发布与动作读取接口。
- `new_core/src/gui/lua/gui_lua51_hook.h/.cpp`：捕获游戏创建的 Lua State，安装和卸载原生绑定，不再主动调用危险的 HOI3 Lua 接口。
- `script/scripted_gui_runtime.lua`：通用 Lua 插件调度器、发布者所有权、刷新策略、错误冷却和动作泵。
- `script/scripted_gui_plugins.lua`：注册需要从游戏读取实时数据的 Lua 插件。
- `script/gui_data_bridge.lua`、`script/gui_action_bridge.lua`：Lua 侧数据发布与动作消费接口。

### 2.5 渲染、资源和特殊可视化

- `new_core/src/gui/d3d9/gui_host_d3d9.h/.cpp`：Windows 游戏内宿主；创建会话、缩放设计坐标、绘制命令、路由鼠标输入并控制底层游戏点击穿透。
- 每个会话的逻辑画布严格等于其根 `windowType` 的 `position + size` 矩形；子控件绘制与输入不能越过根边界。窗口拖动以游戏 D3D9 Viewport 为外边界，根画布触边后停止，不能继续拖出游戏客户区。
- `new_core/src/gui/d3d9/gui_d3d9_hook.h/.cpp`：挂接 `IDirect3D9::CreateDevice`、设备 `Reset/Present/EndScene`、交换链 `Present` 与游戏窗口消息生命周期。
- `new_core/src/gui/d3d9/gui_texture_loader_d3d9.h/.cpp`：加载和缓存 D3D9 图片资源。
- `new_core/src/gui/d3d9/gui_text_renderer_d3d9.h/.cpp`：递归加载 `font` 目录中的 `.ttf/.otf`，使用 GDI+ 生成文字纹理并缓存。
- `new_core/src/gui/runtime/gui_indexed_map_core.h/.cpp`：平台无关的 Region ID 图、着色、边界和命中核心。
- `new_core/src/gui/d3d9/gui_indexed_map_d3d9.h/.cpp`：Windows 索引地图渲染、悬停和点击 Region。
- `new_core/src/gui/d3d9/gui_marker_layer_d3d9.h/.cpp`：Windows 地图 Marker、头像、连线、堆叠、提示、拖动和附属动作。
- `new_core/src/tools/gui_indexed_map_make.cpp`：根据地图源文件离线生成底图和 Region ID 二进制图。

### 2.6 注入、游戏生命周期与持久化

- `new_core/src/dll/scripted_gui_overlay_dll.cpp`、`scripted_gui_overlay.def`：兼容现有 ABI 的薄 DLL 入口和导出接口。
- `new_core/src/launcher/scripted_gui_injector.cpp`：把 New Core DLL 注入指定 HOI3 进程。
- `new_core/src/launcher/new_core_launcher.cpp`、`new_core_launcher_core.h/.cpp`、`new_core/src/core/new_core_handshake.h/.cpp`：最终产品启动器、注入/原版双模式、PE32 校验和版本化握手；旧 `scripted_gui_injector` 仅保留为开发工具。
- `new_core/src/hoi3/hoi3_lifecycle.h/.cpp`：安全识别主菜单与战局状态，并向全部核心模块发布统一生命周期。
- `new_core/src/gui/runtime/gui_persistence.h/.cpp`：按插件、会话和存档边界保存/恢复 GUI 状态。
- `new_core/src/*_probe*.cpp`：离线、集成和回归测试程序，不是最终 DLL 的业务模块。

## 3. 系统完成度

### 3.1 已形成闭环的核心功能

- `.sgui/.sgfx` 解析、资源注册和插件清单加载；
- 窗口、图片、文字、按钮、色块、进度条、列表、滚动条、索引地图、MarkerLayer 和 Custom 扩展控件；
- 父子相对坐标、显式 `parent`、统一 Z 顺序、父级裁剪和列表裁剪；
- 窗口拖动、控件拖动、悬停、按下、释放、点击和拖动事件；
- 通用列表模板实例化、网格布局、半圆/极坐标布局和滚动条自动绑定；
- `visibleWhen`、`enabledWhen`、数据路径插值和动态文字/图片/数值绑定；
- Sprite 固定帧、数据帧、循环/往返/单次动画，以及可选进度条贴图；
- 图片、文字、按钮、色块、进度条和滚动条的统一旋转、缩放、翻转、枢轴与变换后输入命中；
- 声明式内置 `effectType`，支持静态调色、亮度脉冲、透明度脉冲和颜色脉冲，不依赖 HOI3 Effect 文件；
- Lua 数据发布、Lua 动作回调、离线回退、刷新调度和发布者稳定性；
- 多插件、多窗口、窗口打开/关闭、游戏内/主菜单生命周期隔离；
- 持久化状态、会话切换和存档回滚恢复；
- D3D9 游戏内绘制、自适应缩放、输入区域收缩和底层游戏点击穿透；
- `.sgui/.sgfx` 的未知字段、类型、范围、必填项、互斥项和控件适用范围诊断；原版 `.gui/.gfx` 可选择严格模式；
- `fullScreen`、`positionType`、`orientation` 原版兼容布局字段。

中国战争地图是第一个完整实例，议会半圆席位图是第二个纯声明式实例。

### 3.2 系统待补充的核心功能

- **若目标定义为支持由 Lua 数据驱动的通用 Windows D3D9 2D 游戏内 GUI，则核心能力已经补齐。**
- **若目标定义为完整复刻 HOI3/HOI4 全部 GUI 能力，尚未完全补齐。**

仍存在的能力边界：

1. 通用 3D 模型控件与对应资源生命周期尚未实现。
2. `customWidgetType` 仍要求 C++ 注册对应 Handler；它是扩展新控件语义的边界，不是纯配置控件。
3. 内置 `effectType` 是安全的颜色乘算与周期效果抽象，不加载原版 HOI3 `.fx`，也不执行任意 Shader。
4. 几何变换当前面向普通 2D 叶控件；索引地图、MarkerLayer、Custom、列表容器和根窗口不套用该通用变换，以避免其专用坐标系统与输入映射失配。

### 3.3 系统的初步验收结论

“只改 interface/font/gfx/lua 就能添加新 GUI”的验收标准，对内置 2D 控件范围内的界面**已经达到**；对任意新控件语义或 3D 界面则尚未达到。

- 纯静态或文件数据驱动的 2D GUI：不需要新增 C++，但除 `.sgui/.sgfx` 和素材外，至少还要在 `interface/gui_plugins` 添加插件清单；通常还需要 `script_gui/data` 数据文件。
- 读取游戏实时状态、修改游戏状态或响应复杂动作的 GUI：不需要新增 C++ 的前提是现有 Lua/HOI3 接口足够，但仍需新增 Lua 数据模块、动作函数，并在 `script/scripted_gui_plugins.lua` 注册。
- 需要新控件语义、3D 模型、尚未暴露的游戏内存数据或新 Hook：仍需修改 C++。

因此当前已经达到的实际验收目标是：

> 对现有内置控件能够表达的 2D GUI，可以通过 `.sgui + .sgfx + 插件清单 + 数据/行为 Lua + 素材/字体` 完成，不再为每个 GUI 新写一个专用 C++ 程序。

## 4. 系统基础语法

```text
# 注释方式一
// 注释方式二

property = value
property = "带空格的字符串"
property = 123
property = -1.5
property = yes
property = { child = value }
```

- 文件扩展名：解释器会读取 `.gui`、`.gfx`、`.sgui`、`.sgfx`。
- 字段名和控件类型名：匹配时不区分大小写。
- 标识符可包含字母、数字、下划线、连字符和点；包含空格、反斜杠、斜杠或其他字符时应加双引号。
- 字符串内反斜杠是转义符；Windows 路径建议写成 `"gfx\\war_map\\image.png"`。
- 布尔真值：`yes`、`true`、`1`；布尔假值：`no`、`false`、`0`。
- `position`、`size` 等二维块必须使用 `x`、`y`，不支持 `{ 10 20 }` 形式。
- RGB/RGBA 颜色同时支持位置形式 `{ 1.0 0.5 0.2 1.0 }` 和命名形式 `{ r = 1.0 g = 0.5 b = 0.2 a = 1.0 }`。
- 未知字段不会自动报错；它们会被语法树保留，但不会产生运行时效果。

## 5. SGUI 控件类型

| 状态 | 控件语句 | 作用 |
|---|---|---|
| 现用 | `guiTypes = { ... }` | 习惯性的布局容器；解释器递归查找其中的窗口，本身没有运行时对象。 |
| 现用 | `windowType = { ... }` | 窗口或窗口内的子容器；支持边框、拖动、父子布局、条件、裁剪和 Z 顺序。 |
| 现用 | `iconType = { ... }` | 图片控件；可使用固定 Sprite 或数据动态选择 Sprite。 |
| 现用 | `textBoxType = { ... }` | 文字控件。 |
| 已实现未用 | `instantTextBoxType = { ... }` | `textBoxType` 的完全等价别名。 |
| 现用 | `guiButtonType = { ... }` | 按钮；支持普通/按下图片、文字、条件和事件。 |
| 现用 | `listBoxType = { ... }` | 动态列表；列表数据键默认为该控件的 `name`。 |
| 现用 | `scrollbarType = { ... }` | 与列表绑定的滚动条；指定轨道和滑块 Sprite。 |
| 现用 | `progressBarType = { ... }` | 进度条控件；引用 `.sgfx` 中的同名资源定义。 |
| 现用 | `colorBoxType = { ... }` | 纯色矩形。当前控件色只使用 RGB，透明度不由此字段控制。 |
| 现用 | `indexedMapType = { ... }` | Region ID 索引地图；支持数据着色、边界、悬停和点击 ID。 |
| 现用 | `markerLayerType = { ... }` | 附着到索引地图的动态 Marker 列表。 |
| 已实现（需 C++） | `customWidgetType = { ... }` | C++ 自定义控件扩展点；Windows Draw/Input 链路已经接通，但必须先由插件注册同名 Handler，通用声明式插件默认不提供 Handler。 |

所有上述控件均可嵌套在 `windowType` 或其他控件内部。子控件坐标默认相对其词法父控件；也可用 `parent` 改为相对同一窗口中的另一个具名控件。

## 6. SGUI 通用字段

### 6.1 身份、坐标、层级和裁剪

| 状态 | 语句 | 作用 |
|---|---|---|
| 现用 | `name = "id"` | 控件唯一名称；用于父子引用、模板、滚动条、地图、拖动轨道、事件上下文和配置校验。 |
| 已实现未用 | `parent = "other_widget"` | 将当前控件的坐标父级改为同一窗口内的具名控件；找不到时退回词法父级。 |
| 现用 | `position = { x = 10 y = 20 }` | 相对父控件的左上角坐标；根窗口为设计画布坐标。 |
| 现用 | `size = { x = 100 y = 40 }` | 控件宽度和高度。图片宽高小于等于零时，宿主可使用图片原始尺寸。 |
| 现用 | `zOrder = 10` | 相对父控件的 Z 偏移；最终 Z 为父级 Z 与子级 Z 之和。 |
| 已实现未用 | `z = 10` | `zOrder` 的别名。 |
| 已实现未用 | `layer = 10` | `zOrder` 的别名。 |
| 已实现未用 | `clipChildren = yes` | 将所有后代裁剪在当前控件矩形内。 |
| 已实现未用 | `clip_children = yes` | `clipChildren` 的别名。 |
| 已实现未用 | `clip = yes` | `clipChildren` 的别名。 |
| 已实现 | `positionType = { name = "anchor" position = { x = 0 y = 0 } }` | 注册原版风格的具名坐标。名称进入解释器的全局位置注册表，可由控件的字符串形式 `positionType` 引用。 |
| 已实现 | `positionType = "anchor"` / `position_type = "anchor"` | 引用具名坐标；具名坐标与控件自身 `position` 相加后，再交给 `orientation` 计算最终位置。找不到名称时仅使用控件自身 `position`。 |
| 已实现 | `fullScreen = yes/no` | 仅对作为会话根窗口运行的 `windowType` 生效。`yes` 时根矩形实时绑定当前 D3D9 客户区，窗口模式、全屏模式和分辨率变化都会更新根画布；此时宿主不再缩放或拖动整个根画布。 |
| 已实现 | `orientation = "CENTER/UPPER_LEFT/UPPER_RIGHT/LOWER_LEFT/LOWER_RIGHT"` | 以父控件矩形为锚点计算子控件位置。还支持 `CENTRE`、`CENTER_UP/TOP`、`CENTER_DOWN/BOTTOM`、`CENTER_LEFT`、`CENTER_RIGHT`。右侧和下侧锚点把正 `x/y` 解释为距对应边缘的内缩距离。 |

### 6.2 显示、启用和条件

| 状态 | 语句 | 作用 |
|---|---|---|
| 已实现未用 | `visible = yes/no` | 静态控制是否参与渲染和输入，默认 `yes`。 |
| 已实现未用 | `dontRender = yes/no` | `visible` 的反向兼容写法；`yes` 等价于 `visible = no`。 |
| 现用 | `visibleWhen = "condition"` | 每次刷新时求值；为假时控件及其后代不绘制、不接收输入。 |
| 已实现未用 | `visible_if = "condition"` | `visibleWhen` 的别名。 |
| 已实现未用 | `showIf = "condition"` | `visibleWhen` 的别名。 |
| 已实现未用 | `condition = "condition"` | 控件可见条件别名；注意行为文件中同名字段表示行为启用条件。 |
| 已实现未用 | `enabled = yes/no` | 静态控制输入是否启用，默认 `yes`。 |
| 已实现未用 | `opacity = 0.0~1.0` | 控件透明度；与父级透明度相乘。最终透明度为零时不可见且不拦截输入。 |
| 已实现未用 | `alpha = 0.0~1.0` | `opacity` 的兼容别名。 |
| 已实现未用 | `disabled = yes/no` | `enabled` 的反向写法；`yes` 会禁用控件。 |
| 现用 | `enabledWhen = "condition"` | 动态控制输入是否启用；按钮禁用时会以灰色调绘制。 |
| 已实现未用 | `enabled_if = "condition"` | `enabledWhen` 的别名。 |

### 6.3 条件表达式

`visibleWhen`、`enabledWhen` 和行为文件中的 `enabledWhen` 共用同一求值器。

| 表达式 | 作用 |
|---|---|
| `state.active` | 读取布尔数据键。 |
| `!state.active` | 逻辑非。 |
| `a && b` | 逻辑与。 |
| `a || b` | 逻辑或。 |
| `state.tag == CHI` | 字符串或数值文本相等比较，不区分大小写。 |
| `state.tag != JAP` | 不等比较。 |
| `regions.{selectedregion.id}.active` | 先用另一个数据键替换 `{...}`，再读取最终路径；插值最大递归深度为 8。 |
| `(state.active)` | 整个子表达式外的一层括号可用。 |

限制：当前不支持 `<`、`>`、`<=`、`>=`、算术运算和可靠的任意嵌套括号。复杂条件应在 Lua 中预先计算为布尔数据键。

### 6.4 通用事件绑定

字段值是行为名称。若 `script_gui` 中存在同名 `behavior`，按其函数、阶段、条件和参数执行；否则直接尝试调用同名 Lua 动作函数。

| 状态 | 语句 | 作用 |
|---|---|---|
| 现用 | `onClick = "action"` | 鼠标在同一控件按下并释放且未发生拖动时触发。 |
| 已实现未用 | `onclick` / `clickAction` / `action` / `callback` | `onClick` 的别名。 |
| 已实现未用 | `onPress = "action"` | 鼠标按下时触发。 |
| 已实现未用 | `onpress` / `pressAction` | `onPress` 的别名。 |
| 已实现未用 | `onRelease = "action"` | 已按下控件收到鼠标释放时触发。 |
| 已实现未用 | `onrelease` / `releaseAction` | `onRelease` 的别名。 |
| 已实现未用 | `onHoverEnter = "action"` | 鼠标首次进入控件时触发。 |
| 已实现未用 | `onhoverenter` / `onHover` / `onhover` / `onMouseEnter` / `onmouseenter` / `hoverEnterAction` | `onHoverEnter` 的别名。 |
| 已实现未用 | `onHoverLeave = "action"` | 鼠标离开控件时触发。 |
| 已实现未用 | `onhoverleave` / `onMouseLeave` / `onmouseleave` / `hoverLeaveAction` | `onHoverLeave` 的别名。 |
| 已实现未用 | `onDragStart = "action"` | 可拖动控件开始连续移动时触发一次。 |
| 已实现未用 | `ondragstart` | `onDragStart` 的别名。 |
| 现用 | `onDrag = "action"` | 按下和连续拖动期间触发，并附带标准拖动参数。 |
| 已实现未用 | `ondrag` | `onDrag` 的别名。 |
| 现用 | `onDragEnd = "action"` | 拖动结束时触发。 |
| 已实现未用 | `ondragend` | `onDragEnd` 的别名。 |

标准动作上下文包括窗口名、控件名、列表名、列表索引、列表项 ID、鼠标 X/Y。拖动事件还包括：`normalized`、`value`、`dragx`、`dragy`、`deltax`、`deltay`、`stepdeltax`、`stepdeltay`、`target`，配置 `dragSteps` 时还包括 `stepindex`。

### 6.5 可继承样式默认值

任意控件可声明 `styleDefaults = { ... }`。这些值只作用于其后代控件，按控件树逐层继承；更深层的 `styleDefaults` 会覆盖外层默认值，控件自身显式字段具有最高优先级。根 `windowType` 中声明它，可统一管理整个窗口的尺寸阈值和视觉样式。

| 状态 | 语句 | 作用 |
|---|---|---|
| 现用 | `styleDefaults = { ... }` | 为当前控件的后代建立继承样式环境，本身仍使用自己的显式字段。 |
| 现用 | `color = { r g b }` | 默认文字颜色。 |
| 现用 | `lineColor = { r g b a }` | 默认连线颜色。 |
| 现用 | `tooltipColor = { r g b a }` | 默认 Tooltip 背景色。 |
| 已实现未用 | `tooltipTextColor = { r g b }` | 默认通用 Tooltip 文字颜色。 |
| 已实现未用 | `tooltipSize = { x = 240 y = 96 }` | 默认通用 Tooltip 外框尺寸。 |
| 已实现未用 | `tooltipOffset = { x = 12 y = 16 }` | 默认通用 Tooltip 相对鼠标或目标控件的偏移。 |
| 已实现未用 | `tooltipSprite = "GFX_tooltip"` | 默认通用 Tooltip 背景 Sprite。 |
| 已实现未用 | `tooltipFont = "font_name"` | 默认通用 Tooltip 字体。 |
| 已实现未用 | `tooltipFontSize = 18` | 默认通用 Tooltip 字号。 |
| 已实现未用 | `tooltipLineSpacing = 3` | 默认通用 Tooltip 行距。 |
| 已实现未用 | `tooltipPlacement = "cursor"` | 默认通用 Tooltip 放置方式。 |
| 已实现未用 | `tooltipScaleMode = "stretch"` | 默认通用 Tooltip 背景缩放方式。 |
| 已实现未用 | `tooltipNineSlice = { left = 8 top = 8 right = 8 bottom = 8 }` | 默认通用 Tooltip 背景九宫格。 |
| 已实现未用 | `tooltipDelay = 250` | 默认通用 Tooltip 悬停延迟，单位毫秒。 |
| 已实现未用 | `tooltipWrap = yes` | 默认通用 Tooltip 是否换行。 |
| 已实现未用 | `localizeTooltip = yes` | 默认把通用或 Marker Tooltip 文本作为本地化键解析。 |
| 现用 | `frameZOrder = -1000` | 默认窗口框层级偏移。 |
| 已实现未用 | `fontSize = 20` | 默认文字字号。 |
| 已实现未用 | `lineSpacing = 4` | 默认文字行距。 |
| 现用 | `lineWidth = 3` | 默认连线宽度。 |
| 现用 | `tooltipPadding = 12` | 默认 Tooltip 内边距。 |
| 现用 | `tooltipSearchStep = 12` | Marker Tooltip 避让其他 Marker 时的垂直搜索步长。 |
| 现用 | `minimumThumbSize = 18` | 默认滚动条最小滑块尺寸。 |
| 现用 | `disabledBrightness = 0.588235` | 禁用按钮的 RGB 亮度乘数，范围 `0.0~1.0`。 |
| 现用 | `disabledOpacity = 0.588235` | 禁用按钮的透明度乘数，范围 `0.0~1.0`。 |

### 6.6 严格 Schema 与未知字段诊断

严格字段诊断默认对 `.sgui` 和 `.sgfx` 启用。原版 `.gui/.gfx` 默认仍采用宽松兼容模式；启动进程前设置环境变量 `SCRIPTED_GUI_STRICT_LEGACY=1`，即可对原版扩展名启用同一套可选严格模式。C++ 测试或工具也可在加载前调用 `GuiInterpreter::SetStrictLegacyFiles(true)`。

当前诊断覆盖：

1. SGUI/SGFX 根容器与对象类型。
2. 通用控件字段、`styleDefaults`、静态数据声明。
3. Sprite、进度条、内置效果、索引地图资源、`sourceItem` 和 `colorStop`。
4. `position`、`size`、`pivot`、`transformScale`、颜色、九宫格等常用子块的成员名。
5. 字段值类型：标量、块、布尔、整数、浮点数和枚举。
6. 数值范围：颜色、透明度、禁用亮度为 `0~1`，尺寸和内边距不得为负，帧数和资源 ID 等具有各自下限或上下限。
7. 必填项：控件名、非全屏窗口尺寸、列表模板、滚动条滑块与轨道、进度条资源、索引地图资源、自定义控件类型以及资源文件的关键字段。
8. 互斥项：别名重复、静态与动态文本源并存、正反布尔字段并存、静态值与动态值源并存，以及 `fullScreen=yes` 与 `moveable=yes`。
9. 控件适用范围：窗口、列表、进度条、滚动条、索引地图、MarkerLayer 和 Custom 专用字段不能误用于其他控件。
10. 文件路径、源码行号、对象类型与对象名称；拼写接近时附带建议。

示例：

```text
interface/example.sgui:42: unknown field 'visiblWhen' in guiButtonType 'open_button'; did you mean 'visiblewhen'?
```

所有 Schema 问题当前均为非致命加载诊断：错误字段会按解释器现有回退规则处理，诊断可从 `GuiInterpreter::LoadDiagnostics()` 和宿主诊断日志取得。`dataListType.item` 内的业务字段名称保持开放，不参与未知字段诊断。

## 7. SGUI 图片、按钮和窗口字段

### 7.1 Sprite 选择

| 状态 | 语句 | 作用 |
|---|---|---|
| 现用 | `spriteType = "GFX_name"` | 固定普通 Sprite。 |
| 现用 | `quadTextureSprite = "GFX_name"` | `spriteType` 的兼容别名，当前主要用于按钮。 |
| 现用 | `spriteSource = "data.path"` | 从数据注册表或列表项动态取得 Sprite 名。列表模板中可写 `item.field`。 |
| 已实现未用 | `spriteBinding` / `textureSource` | `spriteSource` 的别名。 |
| 现用 | `spriteValuePrefix = "GFX_prefix_"` | 只有动态 Source 成功取值时才添加此前缀。 |
| 已实现未用 | `spritePrefix` | `spriteValuePrefix` 的别名。 |
| 现用 | `pressedTextureSprite = "GFX_name"` | 按下状态的固定 Sprite。 |
| 已实现未用 | `pressedQuadTextureSprite = "GFX_name"` | `pressedTextureSprite` 的别名。 |
| 现用 | `pressedSpriteSource = "data.path"` | 按下状态的动态 Sprite。为空时回退普通 Sprite。 |
| 已实现未用 | `pressedTextureSource` | `pressedSpriteSource` 的别名。 |
| 现用 | `borderSprite = "GFX_name"` | 窗口框 Sprite；也作为 `frameSprite` 的首选别名。 |
| 现用 | `frameSprite = "GFX_name"` | `borderSprite` 的别名；Marker 中用于每个 Marker 的框。 |
| 已实现未用 | `windowFrame = "GFX_name"` | `borderSprite` 的别名。 |
| 已实现未用 | `scaleMode = "stretch/contain/preserve/preserveAspect/aspect/center/none"` | Windows D3D9 支持拉伸、保持比例或居中裁切；适用于窗口框、图片、按钮和滚动条贴图。 |
| 已实现未用 | `scale` / `fit` | `scaleMode` 的别名。 |
| 已实现未用 | `nineSlice = { left = 8 top = 8 right = 8 bottom = 8 }` | Windows D3D9 九宫格；边角保持尺寸，边缘和中心拉伸。适用于窗口框、图片、按钮和滚动条贴图。 |
| 已实现未用 | `nine_slice = { ... }` | `nineSlice` 的兼容别名。 |

### 7.2 Sprite 帧与动画

`frame` 与动画字段适用于 `iconType` 和 `guiButtonType`。帧编号与原版 HOI3 一致，从 **1** 开始；运行时优先级为 `frameSource > 动画 > frame`。

| 状态 | 语句 | 作用 |
|---|---|---|
| 已实现未用 | `frame = 1` | 固定选择 Sprite Sheet 的第几帧；超出范围时夹紧到 `1..noOfFrames`。 |
| 已实现未用 | `frameSource = "data.path"` | 从数据注册表动态读取帧号；列表模板中支持 `item.field` 和 `{id}`。非整数按最接近整数处理。 |
| 已实现未用 | `frameBinding` | `frameSource` 的别名。 |
| 已实现未用 | `animate = yes/no` | 显式开启或关闭自动动画；未写时继承 Sprite 资源的 `animation` 设置。 |
| 已实现未用 | `animationMode = "loop/pingpong/once"` | 覆盖资源动画模式：循环、往返或播放一次后停在末帧。 |
| 已实现未用 | `animationFrameTime = 100` | 每帧持续毫秒数。 |
| 已实现未用 | `animationFrameDuration = 100` | `animationFrameTime` 的别名。 |
| 已实现未用 | `animationFps = 12` | 用每秒帧数指定速度；与上面两个毫秒字段互斥。 |
| 已实现未用 | `animationStartFrame = 1` | 覆盖动画起始帧；`0` 表示继承资源。 |
| 已实现未用 | `animationEndFrame = 8` | 覆盖动画结束帧；`0` 表示继承资源或使用最后一帧。 |
| 已实现未用 | `animationOffset = 50` | 动画时间偏移，单位毫秒；可为负数。 |
| 已实现未用 | `animationTimeSource = "state.time_ms"` | 用数据值作为动画时钟，单位毫秒；适合由 Lua 同步、暂停或精确控制动画。未设置时使用窗口会话时钟。 |

```gui
iconType = {
    name = "animated_warning"
    spriteType = "GFX_animated_warning"
    animate = yes
    animationMode = "pingpong"
    animationFps = 10
    position = { x = 20 y = 20 }
    size = { x = 64 y = 64 }
}
```

多帧裁切与 `stretch`、`contain`、`center` 和 `nineSlice` 均兼容。按钮普通 Sprite 与按下 Sprite 使用同一个帧选择/动画配置，并分别按各自资源的 `noOfFrames` 夹紧。

### 7.3 通用 2D 几何变换

几何变换适用于 `iconType`、`textBoxType`/`instantTextBoxType`、`guiButtonType`、`colorBoxType`、`progressBarType` 和 `scrollbarType`。绘制与输入使用同一份最终变换：旋转或缩放后的按钮不会继续使用旧矩形命中。变换只作用于当前控件，不自动传递给子控件。

| 状态 | 语句 | 作用 |
|---|---|---|
| 已实现未用 | `rotation = 15` | 当前控件顺时针旋转角度，单位为度；可为任意实数。 |
| 已实现未用 | `rotationSource = "data.path"` | 从数据注册表动态读取旋转角；列表模板支持 `item.field` 和 `{id}`。 |
| 已实现未用 | `pivot = { x = 0.5 y = 0.5 }` | 旋转、缩放和翻转枢轴，使用控件矩形内的归一化坐标；`0,0` 为左上角，`1,1` 为右下角。 |
| 已实现未用 | `transformScale = { x = 1.0 y = 1.0 }` | X/Y 静态几何缩放，范围 `0.001~100`。它不同于图片适配字段 `scaleMode` 及其别名 `scale`。 |
| 已实现未用 | `scaleSource = "data.path"` | 动态统一缩放，同时覆盖 X/Y；`transformScaleSource` 是等价别名。 |
| 已实现未用 | `scaleXSource = "data.path"` | 动态 X 缩放；在统一动态缩放之后覆盖 X。`transformScaleXSource` 是等价别名。 |
| 已实现未用 | `scaleYSource = "data.path"` | 动态 Y 缩放；在统一动态缩放之后覆盖 Y。`transformScaleYSource` 是等价别名。 |
| 已实现未用 | `flipX = yes` | 围绕 `pivot` 水平翻转。 |
| 已实现未用 | `flipY = yes` | 围绕 `pivot` 垂直翻转。 |

```gui
guiButtonType = {
    name = "rotating_button"
    spriteType = "GFX_rotating_button"
    rotationSource = "state.button_angle"
    pivot = { x = 0.5 y = 0.5 }
    transformScale = { x = 1.1 y = 1.1 }
    onClick = "activate_button"
    position = { x = 100 y = 100 }
    size = { x = 160 y = 48 }
}
```

九宫格的九个切片会以完整控件矩形为共同枢轴整体变换，不会分别旋转。`contain` 和 `center` 产生的实际图片矩形也仍以原控件矩形的 `pivot` 为枢轴。变换后的图形继续受父级轴对齐裁剪矩形约束。

### 7.4 窗口与按钮

| 状态 | 语句 | 作用 |
|---|---|---|
| 现用 | `frameZOrder = -1000` | 窗口框相对窗口最终 Z 的额外偏移；负值可把背景框放到所有子控件后方。 |
| 现用 | `moveable = yes` | 允许拖动该窗口。 |
| 现用 | `dragHeight = 72` | 窗口顶部可拖动区域高度；必须与 `moveable = yes` 同时使用。 |
| 现用 | `font`、`fontSize`、`alignment`、`color` | 按钮没有文字子控件时，可直接绘制按钮文字；列表按钮默认使用列表项 `text`。 |
| 现用 | `disabledBrightness = 0.588235` | 按钮禁用时的 RGB 亮度乘数；可由 `styleDefaults` 继承。 |
| 现用 | `disabledOpacity = 0.588235` | 按钮禁用时的透明度乘数；可由 `styleDefaults` 继承。 |

## 8. SGUI 文字字段

| 状态 | 语句 | 作用 |
|---|---|---|
| 已实现未用 | `text = "literal"` | 静态文字；优先级低于动态 `textSource` 和 `localizationKey`。 |
| 现用 | `textSource = "data.path"` | 动态文字数据路径；列表模板中支持 `item.field` 和 `{id}`。 |
| 已实现未用 | `textBinding` / `textValue` | `textSource` 的别名。 |
| 现用 | `localizationKey = "KEY"` | 直接把指定本地化键解析为文本，优先于 `textSource` 的显示结果。 |
| 已实现未用 | `localisationKey` / `textKey` | `localizationKey` 的别名。 |
| 现用 | `localized = yes` | 把 `text` 或 `textSource` 的结果再次当作本地化键解析。 |
| 已实现未用 | `localised = yes` | `localized` 的别名。 |
| 现用 | `font = "file_stem"` | 选择字体；值对应 `font` 目录下 `.ttf/.otf` 文件名去掉扩展名后的名称，不区分大小写。 |
| 现用 | `fontSize = 20` | 像素字号；未设置时默认取 `max(12, 控件高度 × 2/3)`。 |
| 已实现未用 | `textSize = 20` | `fontSize` 的别名。 |
| 现用 | `alignment = "left/center/centre/right"` | 水平对齐；未知值回退左对齐。 |
| 已实现未用 | `textAlignment` / `align` | `alignment` 的别名。 |
| 现用 | `color = { r g b }` | 文字 RGB，分量范围通常为 `0.0` 到 `1.0`。 |
| 现用 | `wrap = yes` | 在文字矩形内换行。 |
| 已实现未用 | `wordWrap = yes` | `wrap` 的别名。 |
| 现用 | `lineSpacing = 4` | 多行文字附加行距。 |
| 部分实现 | `renderMode = "custom"` | 当前只会阻止通用文字渲染器绘制该文字；不会自动调用自定义渲染器。 |
| 部分实现 | `drawMode = "custom"` | `renderMode` 的别名。 |

字体无需安装到操作系统。Windows 宿主会递归加载 `font` 目录；找不到指定字体时回退通用 Sans Serif。

### 8.1 通用 Tooltip

除 `markerLayerType` 外，所有可见且启用的普通控件都可声明通用 Tooltip。Tooltip 位于窗口最终绘制层，不参与命中测试，因此不会吞掉控件点击；其外框始终限制在根窗口画布内。列表模板中的 Tooltip 支持 `item.field` 和 `{id}`。

| 状态 | 语句 | 作用 |
|---|---|---|
| 已实现未用 | `tooltipText = "literal"` | 静态 Tooltip 文本。 |
| 已实现未用 | `tooltip = "literal"` | `tooltipText` 的别名。 |
| 已实现未用 | `delayedTooltipText = "literal"` | 静态文本兼容别名；是否延迟仍由 `tooltipDelay` 决定。 |
| 已实现未用 | `tooltipSource = "data.path"` | 从数据注册表读取文本；列表模板可写 `item.field`，路径可含 `{id}`。动态结果为空时回退静态文本。 |
| 已实现未用 | `tooltipBinding` / `tooltipValue` | `tooltipSource` 的别名。 |
| 已实现未用 | `tooltipLocalizationKey = "KEY"` | 直接解析本地化键，优先于静态文本和动态 Source。 |
| 已实现未用 | `tooltipTextKey` | `tooltipLocalizationKey` 的别名。 |
| 已实现未用 | `localizeTooltip = yes` | 把最终文本再次作为本地化键解析。 |
| 已实现未用 | `localiseTooltip` | `localizeTooltip` 的别名。 |
| 已实现未用 | `tooltipSize = { x = 240 y = 96 }` | Tooltip 外框尺寸；配置 Tooltip 时必须为正数。 |
| 已实现未用 | `tooltipOffset = { x = 12 y = 16 }` | 相对鼠标或目标控件锚点的 X/Y 偏移。 |
| 已实现未用 | `tooltipPlacement = "cursor/right/left/top/bottom"` | `cursor` 跟随鼠标；其他值相对目标控件对应边缘放置。未知值按 `cursor`。 |
| 已实现未用 | `tooltipSide` | `tooltipPlacement` 的别名。 |
| 已实现未用 | `tooltipDelay = 250` | 鼠标持续停留多少毫秒后显示；默认 `0`。 |
| 已实现未用 | `tooltipSprite = "GFX_tooltip"` | 背景 Sprite；未设置时使用 `tooltipColor` 绘制纯色矩形。 |
| 已实现未用 | `tooltipBackgroundSprite` | `tooltipSprite` 的别名。 |
| 已实现未用 | `tooltipColor = { r g b a }` | 背景颜色或 Sprite 调色，最终透明度还会乘以控件继承透明度。 |
| 已实现未用 | `tooltipScaleMode = "stretch/contain/center"` | 背景 Sprite 缩放；兼容 `preserve/preserveAspect/aspect/none`。 |
| 已实现未用 | `tooltipNineSlice = { left = 8 top = 8 right = 8 bottom = 8 }` | 背景 Sprite 九宫格；启用时优先于 `tooltipScaleMode`。 |
| 已实现未用 | `tooltip_nine_slice = { ... }` | `tooltipNineSlice` 的别名。 |
| 已实现未用 | `tooltipPadding = 10` | 文字相对外框的内边距。 |
| 已实现未用 | `tooltipFont = "font_name"` | Tooltip 专用字体；未设置时回退控件 `font`。 |
| 已实现未用 | `tooltipFontSize = 18` | Tooltip 专用字号；未设置时回退控件 `fontSize`，两者至少一个必须为正数。 |
| 已实现未用 | `tooltipTextColor = { r g b }` | Tooltip 文字 RGB。 |
| 已实现未用 | `tooltipLineSpacing = 3` | Tooltip 行距；为 `0` 时回退控件 `lineSpacing`。 |
| 已实现未用 | `tooltipWrap = yes` | 是否在 Tooltip 文字矩形内换行。 |

最小示例：

```gui
guiButtonType = {
    name = "example_button"
    tooltipSource = "items.{id}.description"
    tooltipSize = { x = 260 y = 90 }
    tooltipOffset = { x = 14 y = 18 }
    tooltipPlacement = "cursor"
    tooltipSprite = "GFX_common_tooltip"
    tooltipNineSlice = { left = 10 top = 10 right = 10 bottom = 10 }
    tooltipFont = "pixel_china"
    tooltipFontSize = 18
    tooltipTextColor = { 0.95 0.95 0.90 }
    tooltipPadding = 12
    tooltipDelay = 200
}
```

## 9. SGUI 拖动字段

这些字段可用于 `iconType`、`guiButtonType` 或其他可命中的控件。

| 状态 | 语句 | 作用 |
|---|---|---|
| 现用 | `draggable = yes` | 启用通用控件拖动。 |
| 现用 | `dragAxis = "horizontal/x & vertical/y"` | 拖动轴；除 `vertical/y` 外默认按水平处理。 |
| 已实现未用 | `dragOrientation` | `dragAxis` 的别名。 |
| 现用 | `dragTrack = "widget_name"` | 用具名控件矩形作为拖动范围。未设置时使用控件自身矩形。 |
| 已实现未用 | `dragBounds` / `trackWidget` | `dragTrack` 的别名。 |
| 现用 | `dragValueSource = "data.path"` | 将数据值映射为当前位置，并把目标路径作为动作参数 `target`。 |
| 已实现未用 | `dragBinding` | `dragValueSource` 的别名。 |
| 现用 | `dragMinimum = 0` | 拖动值最小值。 |
| 现用 | `dragMaximum = 1` | 拖动值最大值。 |
| 已实现未用 | `dragStep = 0.25` | 将拖动值吸附到固定数值步长。 |
| 现用 | `dragSteps = 9` | 把归一化位置转换为从 1 开始的离散 `stepindex`。 |
| 已实现未用 | `dragInverted = yes` | 反转拖动方向和归一化值。 |

## 10. SGUI 列表与滚动条字段

### 10.1 `listBoxType`

| 状态 | 语句 | 作用 |
|---|---|---|
| 现用 | `name = "list_name"` | 同时是控件名和默认列表数据键。当前通用列表不使用 `dataSource` 改名。 |
| 现用 | `itemTemplate = "template_widget"` | 指向同一窗口内的具名模板控件；模板根及全部子控件会为每个列表项实例化。 |
| 现用 | `scrollbarType = "scrollbar_name"` | 绑定具名 `scrollbarType` 控件。字段名不区分大小写。 |
| 现用 | `spacing = 6` | 相邻行之间的垂直间隔。 |
| 现用 | `columnSpacing = 8` | 相邻列之间的水平间隔。 |
| 现用 | `layout = "polar"` | 列表布局模式；默认是按控件宽度自动计算列数的网格。 |
| 已实现未用 | `layoutMode` / `itemLayout` | `layout` 的别名。 |
| 现用 | `disableItemsInList = "other_list"` | 若当前项 ID 已存在于另一列表，则禁用当前项。 |
| 已实现未用 | `disabledByList` | `disableItemsInList` 的别名。 |
| 现用 | `disableMatchingField = "field"` | 使用字段值而非只按 ID 匹配禁用项。 |
| 已实现未用 | `disabledMatchField` | `disableMatchingField` 的别名。 |
| 现用 | `disableFilterField = "field"` | 在另一列表中额外检查的过滤字段。 |
| 已实现未用 | `disabledFilterField` | `disableFilterField` 的别名。 |
| 现用 | `disableFilterValueSource = "data.path"` | 过滤字段必须等于该数据路径的当前值。 |
| 已实现未用 | `disabledFilterValueSource` | `disableFilterValueSource` 的别名。 |
| 现用 | `itemFilterField = "tag"` | 读取每个列表项的指定字段，只实例化符合过滤值的项目；被过滤项目不占据布局和滚动高度。 |
| 现用 | `itemFilterValueSource = "state.viewertag"` | 从统一数据注册表读取当前过滤值。必须和 `itemFilterField` 同时使用。 |
| 已实现未用 | `filterField` / `filterValueSource` | 上述两个字段的简写别名。 |

列表项还可在数据中提供 `visible`、`visiblewhen`、`enabled` 或 `enabledwhen` 字段。前两者决定是否实例化，后两者只决定是否允许输入。

### 10.2 网格布局

- 模板 `size.x` 和 `columnSpacing` 决定可容纳列数。
- 模板 `size.y` 和 `spacing` 决定行高。
- 超出 `listBoxType.size.y` 的内容通过绑定滚动条滚动。
- 列表自动裁剪实例和模板子控件，不要求额外设置 `clipChildren`。

### 10.3 极坐标/半圆布局

当 `layout` 为 `polar`、`radial` 或 `semicircle` 时，三者目前使用相同的极坐标算法。

| 状态 | 语句 | 作用 |
|---|---|---|
| 现用 | `polarCenter = { x = 225 y = 195 }` | 相对列表左上角的极坐标中心。 |
| 已实现未用 | `polarCenterX = 225` | 单独设置中心 X；会被 `polarCenter.x` 覆盖。 |
| 已实现未用 | `polarCenterY = 195` | 单独设置中心 Y；会被 `polarCenter.y` 覆盖。 |
| 已实现未用 | `polarRingCount = 6` | 未提供 `polarRingItemCounts` 时，自动平均分配的环数。 |
| 现用 | `polarInnerRadius = 58` | 第一圈半径。 |
| 现用 | `polarOuterRadius = 133` | 最大半径；小于等于零时按列表尺寸自动计算。 |
| 现用 | `polarRingSpacing = 15` | 环间距；未设置且多于一圈时自动计算。 |
| 现用 | `polarRingItemCounts = { 12 16 19 }` | 每一圈的项目数；不足时剩余项追加到最后一圈。 |
| 现用 | `polarStartAngle = 180` | 起始角度，单位为度。 |
| 现用 | `polarEndAngle = 360` | 结束角度，单位为度。 |

### 10.4 `scrollbarType`

| 状态 | 语句 | 作用 |
|---|---|---|
| 现用 | `slider = "GFX_thumb"` | 滚动条滑块 Sprite。 |
| 现用 | `track = "GFX_track"` | 滚动条轨道 Sprite。 |
| 现用 | `position`、`size` | 滚动条轨道区域；滑块高度和位置按内容长度自动计算。 |
| 现用 | `minimumThumbSize = 18` | 自动计算后允许的最小滑块尺寸；可由 `styleDefaults` 继承。设置为 0 表示不额外限制。 |

只有列表内容超出视口时，系统才绘制滚动条。

## 11. SGUI 进度条与色块字段

### 11.1 `progressBarType`

| 状态 | 语句 | 作用 |
|---|---|---|
| 现用 | `progressBar = "resource_name"` | 引用 `.sgfx` 中的 `progressBarType` 资源。 |
| 已实现未用 | `progressbar` / `progressResource` / `progressType` | `progressBar` 的别名。 |
| 现用 | `valueSource = "data.path"` | 读取 `0.0` 到 `1.0` 的动态进度；运行时会夹紧到该范围。 |
| 已实现未用 | `valueBinding` / `progressSource` | `valueSource` 的别名。 |
| 已实现未用 | `value = 0.5` | 不使用 `valueSource` 时的静态进度。 |
| 现用 | `progressColor = 0/1` | `0` 使用资源 `color/textureFile1`，`1` 使用 `colortwo/textureFile2`。贴图不存在或加载失败时回退对应纯色。 |
| 已实现未用 | `colorIndex = 0/1` | `progressColor` 的别名。 |
| 现用 | `fillFromEnd = yes` | 从右侧或底部反向填充。 |
| 已实现未用 | `reverse = yes` | `fillFromEnd` 的别名。 |
| 现用 | `drawBackground = yes/no` | 字段已解析并保留；Windows D3D9 当前不生成独立的进度条背景层，背景图片应由独立 `iconType` 提供。 |

### 11.2 `colorBoxType`

| 状态 | 语句 | 作用 |
|---|---|---|
| 现用 | `color = { r g b }` | 纯色矩形 RGB；最终 Alpha 使用控件继承后的 `opacity`。 |
| 现用 | `position`、`size`、`zOrder`、条件字段 | 控制色块范围、层级和显隐。 |

## 12. SGUI 索引地图字段

### 12.1 `indexedMapType`

| 状态 | 语句 | 作用 |
|---|---|---|
| 现用 | `mapResource = "GFX_map"` | 引用 `.sgfx` 中的 `indexedMapResourceType`。 |
| 已实现未用 | `indexedMap` / `indexedMapResource` / `resource` | `mapResource` 的别名。 |
| 现用 | `valueSource = "regions.{id}.value"` | 对每个非零 Region ID 替换 `{id}`，读取数值并按 `colorStop` 着色。 |
| 现用 | `onClick = "action"` | 点击有效 Region 时触发，事件会附带 Region ID 作为项目 ID。 |
| 现用 | `position`、`size` | 地图目标矩形；底图、覆盖层、边界层和悬停层使用同一矩形。 |

## 13. SGUI Marker 图层字段

### 13.1 数据与地图锚点

| 状态 | 语句 | 作用 |
|---|---|---|
| 现用 | `dataSource = "list_name"` | Marker 数据列表。 |
| 已实现未用 | `listSource` / `itemsSource` | `dataSource` 的别名。 |
| 现用 | `mapWidget = "indexed_map_name"` | 指定作为锚点的 `indexedMapType`；为空时使用找到的第一个索引地图。 |
| 已实现未用 | `targetMap` / `indexedMapWidget` | `mapWidget` 的别名。 |
| 现用 | `regionSource = "item.regionid"` | 从列表项读取 Region ID，并取得该 Region 在索引地图中的中心锚点。 |
| 已实现未用 | `regionIdSource` / `anchorItemSource` | `regionSource` 的别名。 |
| 现用 | `xSource = "item.x"` | 读取 Marker 的归一化 X；有效范围为 `0.0` 到 `1.0`。无效时回退 Region 锚点。 |
| 已实现未用 | `markerXSource` | `xSource` 的别名。 |
| 现用 | `ySource = "item.y"` | 读取 Marker 的归一化 Y。 |
| 已实现未用 | `markerYSource` | `ySource` 的别名。 |

### 13.2 外观、头像和连线

| 状态 | 语句 | 作用 |
|---|---|---|
| 现用 | `frameSprite = "GFX_frame"` | 每个 Marker 的框 Sprite。 |
| 现用 | `portraitSource = "item.portrait"` | 每个 Marker 的头像 Sprite 数据字段。 |
| 已实现未用 | `imageSource` / `itemSpriteSource` | `portraitSource` 的别名。 |
| 现用 | `markerSize = { x = 68 y = 84 }` | Marker 总尺寸；默认 `68×84`。 |
| 现用 | `portraitPosition = { x = 2 y = 2 }` | 头像在 Marker 内的相对位置。 |
| 现用 | `portraitSize = { x = 64 y = 80 }` | 头像尺寸；未设置时回退 Marker 尺寸。 |
| 现用 | `lineColor = { r g b a }` | 锚点到 Marker 的连线颜色。 |
| 现用 | `lineWidth = 3` | 连线宽度，最小为 1。 |

### 13.3 堆叠

| 状态 | 语句 | 作用 |
|---|---|---|
| 现用 | `stackSource = "item.group"` | 同值 Marker 归入同一堆叠组；为空时每项独立。 |
| 已实现未用 | `markerStackSource` / `stackGroupSource` | `stackSource` 的别名。 |
| 现用 | `stackOrderSource = "item.order"` | 同组内排序值。 |
| 已实现未用 | `markerStackOrderSource` | `stackOrderSource` 的别名。 |
| 现用 | `stackDirection = "vertical/horizontal"` | `horizontal` 横向堆叠，其他值按纵向处理。 |
| 已实现未用 | `markerStackDirection` | `stackDirection` 的别名。 |
| 现用 | `stackSpacing = 4` | 相邻 Marker 额外间距。 |
| 已实现未用 | `markerStackSpacing` | `stackSpacing` 的别名。 |

### 13.4 Tooltip

| 状态 | 语句 | 作用 |
|---|---|---|
| 现用 | `nameSource = "item.namekey"` | Tooltip 标题数据字段。 |
| 已实现未用 | `titleSource` | `nameSource` 的别名。 |
| 现用 | `descriptionSource = "item.descriptionkey"` | Tooltip 正文数据字段。 |
| 已实现未用 | `markerDescriptionSource` | `descriptionSource` 的别名；`tooltipSource` 已保留给普通控件的通用 Tooltip。 |
| 现用 | `localizeTooltip = yes` | 将标题和正文作为本地化键解析。 |
| 已实现未用 | `localiseTooltip` | `localizeTooltip` 的别名。 |
| 现用 | `tooltipSize = { x = 300 y = 150 }` | Tooltip 尺寸；配置标题或正文 Source 时必须为正数。 |
| 现用 | `tooltipPlacement = "right"` | `right` 放在 Marker 右侧，其他值放在左侧。 |
| 已实现未用 | `tooltipSide` | `tooltipPlacement` 的别名。 |
| 现用 | `avoidTooltipOverlap = yes` | 尝试调整 Tooltip Y，避免覆盖 Marker。 |
| 已实现未用 | `tooltipAvoidMarkers` | `avoidTooltipOverlap` 的别名。 |
| 现用 | `tooltipColor = { r g b a }` | Tooltip 背景 RGBA。 |
| 现用 | `tooltipPadding = 12` | Tooltip 内边距。 |
| 现用 | `font`、`fontSize`、`lineSpacing`、`color` | Tooltip 文字字体、字号、行距和 RGB。 |

### 13.5 Marker 附属动作

| 状态 | 语句 | 作用 |
|---|---|---|
| 现用 | `markerActionSprite = "GFX_action"` | Marker 旁附属按钮 Sprite。 |
| 已实现未用 | `selectedActionSprite` | `markerActionSprite` 的别名。 |
| 现用 | `markerActionPosition = { x = -80 y = 0 }` | 附属按钮相对 Marker 的位置。 |
| 现用 | `markerActionSize = { x = 80 y = 15 }` | 附属按钮尺寸。 |
| 现用 | `onMarkerAction = "action"` | 点击附属按钮时触发的动作。 |
| 已实现未用 | `markerAction` / `selectedAction` | `onMarkerAction` 的别名。 |
| 现用 | `markerActionLocalizationKey = "KEY"` | 附属按钮文字本地化键。 |
| 已实现未用 | `markerActionTextKey` | `markerActionLocalizationKey` 的别名。 |
| 现用 | `markerActionFontSize = 11` | 附属按钮文字字号。 |
| 现用 | `draggable = yes`、`onDragEnd = "action"` | 允许玩家拖动 Marker；事件附带归一化坐标 `normalizedx`、`normalizedy` 等 Marker 专用参数。 |

## 14. SGUI 自定义控件字段

| 状态 | 语句 | 作用 |
|---|---|---|
| 已实现（需 C++） | `customWidgetType = { ... }` | 创建自定义控件节点，并接入 Windows 的统一绘制队列、裁剪、Z 顺序和输入分发。 |
| 已实现（需 C++） | `customType = "handler_name"` | 选择 C++ `GuiCustomWidgetRegistry` 中注册的 Handler；名称区分大小写。 |
| 已实现（需 C++） | `type = "handler_name"` | `customType` 的别名。 |

**注意：如果没有注册匹配 Handler，自定义控件既不会绘制，也不会处理输入。`customWidgetType` 是 C++ 扩展点，不是纯配置控件；只有使用第 4—7 章已有内置控件时，才能仅靠 `.sgui/.sgfx` 添加界面。**

### 14.1 什么时候应创建自定义控件

适合使用 `customWidgetType` 的功能：

1. 动态曲线图、节点图、复杂战场标记层等不能由普通图片和文字组合表达的画布。
2. 需要特殊 D3D9 绘制、着色器或独立纹理更新策略的控件。
3. 需要自行解释鼠标拖拽、滚轮、键盘、文本输入或焦点的交互控件。
4. 后续的 3D 模型预览等拥有独立渲染生命周期的控件。

普通窗口、图片、文字、按钮、列表、滚动条和进度条应优先使用内置控件，不要为它们重复编写 Custom Handler。

### 14.2 第一步：在 `.sgui` 声明控件

```text
customWidgetType = {
    name = "strategic_chart_widget"
    customType = "strategic_chart"

    position = { x = 120 y = 90 }
    size = { x = 640 y = 320 }
    zOrder = 30
    opacity = 1.0

    visibleWhen = "state.active"
    enabledWhen = "state.chart_enabled"
}
```

字段说明：

1. `name` 是这个控件实例的唯一名称，供布局、诊断和事件上下文识别。
2. `customType` 是 Handler 注册键；上例必须注册名为 `strategic_chart` 的 Handler。
3. 若省略 `customType`，解释器会使用 `name` 查找 Handler。
4. `position`、`size`、`zOrder`、`opacity`、父子相对坐标、`clipChildren`、`visibleWhen` 和 `enabledWhen` 都继续由通用解释器处理。
5. Custom Handler 接收的是已经完成布局、缩放、条件计算和裁剪计算的 `GuiResolvedWidget`，不应再次解析 `.sgui`。
6. `onClick`、`onHover` 等通用动作字段可以与 Custom Handler 并存；控件特有的复杂输入由 Handler 的统一事件回调处理。

### 14.3 第二步：实现 Windows Handler

建议为每一种新控件建立独立的 `.h/.cpp`，并提供一个创建 Handler 的工厂函数：

```cpp
#include <d3d9.h>

#include "gui_custom_widget.h"

struct StrategicChartState
{
    int selectedNode = -1;
    bool dragging = false;
};

gui::GuiCustomWidgetHandler CreateStrategicChartHandler()
{
    gui::GuiCustomWidgetHandler handler;

    handler.draw = [](
        const gui::GuiResolvedWidget& widget,
        const gui::GuiCustomWidgetContext& context
    ) {
        auto* device = static_cast<IDirect3DDevice9*>(
            context.graphicsContext
        );
        auto* state = static_cast<StrategicChartState*>(
            context.hostContext
        );
        if (!device || !state)
        {
            return;
        }

        // 由新控件实现者编写；使用 widget.rect、widget.opacity
        // 和 widget.clipRect 绘制，不要重新计算布局。
        DrawStrategicChart(*device, widget, *state);
    };

    handler.event = [](
        const gui::GuiResolvedWidget& widget,
        const gui::GuiCustomWidgetContext& context,
        const gui::GuiCustomInputEvent& event
    ) -> bool {
        auto* state = static_cast<StrategicChartState*>(
            context.hostContext
        );
        if (!state)
        {
            return false;
        }

        switch (event.type)
        {
        case gui::GuiCustomInputEventType::PointerDown:
            if (event.button != gui::GuiCustomPointerButton::Left)
            {
                return false;
            }
            state->dragging = true;
            state->selectedNode = HitTestStrategicChart(
                widget,
                event.mouseX,
                event.mouseY
            );
            return true;

        case gui::GuiCustomInputEventType::PointerMove:
            if (!state->dragging)
            {
                return false;
            }
            UpdateStrategicChartDrag(
                widget,
                event.mouseX,
                event.mouseY,
                *state
            );
            return true;

        case gui::GuiCustomInputEventType::PointerUp:
            if (event.button != gui::GuiCustomPointerButton::Left)
            {
                return false;
            }
            state->dragging = false;
            return true;

        case gui::GuiCustomInputEventType::PointerWheel:
            ZoomStrategicChart(event.wheelDelta, *state);
            return true;

        case gui::GuiCustomInputEventType::FocusLost:
        case gui::GuiCustomInputEventType::Cancel:
            state->dragging = false;
            return false;

        default:
            return false;
        }
    };

    return handler;
}
```

`DrawStrategicChart`、`HitTestStrategicChart`、`UpdateStrategicChartDrag` 和 `ZoomStrategicChart` 是示例控件自己的函数，不是解释器内置 API。Handler 的关键规则如下：

1. `handler.draw` 是必填项；缺少它时 `GuiCustomWidgetRegistry::Register` 会返回 `false`，即使该控件只想处理输入也必须提供空绘制回调。
2. `context.graphicsContext` 在 Windows D3D9 宿主中是借用的 `IDirect3DDevice9*`。
3. `context.hostContext` 由当前插件的 `CustomWidgetContext()` 提供，适合传入控件状态、纹理缓存或服务对象。
4. `event.mouseX/mouseY` 是 GUI 设计画布坐标，可直接与 `widget.rect` 比较。
5. 事件回调返回 `true` 表示事件已消费，Windows 宿主会阻止该事件继续穿透到底层 HOI3。
6. `PointerDown` 返回 `true` 会建立鼠标捕获；之后拖出控件仍会收到移动和释放事件。
7. `PointerUp`、`PointerLeave`、`FocusLost` 和 `Cancel` 属于清理事件，即使控件刚刚变为隐藏或禁用，也可能收到它们；必须在这些分支中清除拖拽、按下等临时状态。

### 14.4 第三步：由插件注册 Handler 和状态

自定义控件必须在插件初始化流程中注册：

```cpp
class StrategicGuiPlugin final : public IGuiPlugin
{
public:
    void RegisterCustomWidgets(
        gui::GuiCustomWidgetRegistry& registry
    ) override
    {
        registry.Register(
            "strategic_chart",
            CreateStrategicChartHandler()
        );
    }

    void* CustomWidgetContext() override
    {
        return &chartState_;
    }

    // 其余 IGuiPlugin 接口按插件需求实现。

private:
    StrategicChartState chartState_;
};
```

注册字符串必须与 `.sgui` 的 `customType` 完全一致。当前注册表采用同名覆盖：再次注册相同名称时，后注册的 Handler 会替换旧 Handler。

当前 `DeclarativeGuiPlugin::RegisterCustomWidgets()` 默认不注册任何 Handler。因此，新增真正的自定义控件时，需要建立实现 `IGuiPlugin` 的专用插件，或者以后为声明式插件增加可配置的 Handler 提供器；仅把 `customWidgetType` 写进使用 `factory = "declarative_gui"` 的清单不会自动得到新语义。

### 14.5 第四步：注册插件工厂并在清单中使用

在 `gui_builtin_plugins.cpp` 注册专用插件工厂：

```cpp
bool RegisterBuiltinGuiPluginFactories(
    GuiPluginRegistry& registry
)
{
    const bool declarativeRegistered = registry.RegisterFactory(
        "declarative_gui",
        CreateDeclarativeGuiPlugin
    );
    const bool strategicRegistered = registry.RegisterFactory(
        "strategic_gui",
        CreateStrategicGuiPlugin
    );
    return declarativeRegistered && strategicRegistered;
}
```

随后在 `interface/gui_plugins/<gui_name>.txt` 选择该工厂：

```text
guiPlugins = {
    guiPlugin = {
        id = "strategic_example"
        displayName = "Strategic Example"
        factory = "strategic_gui"
        startup = yes
        windowZOrder = 20
        modal = no
        maxViewportWidthRatio = 0.92
        maxViewportHeightRatio = 0.90
        cascadeOffsetX = 24
        cascadeOffsetY = 24

        options = {
            window = "strategic_example_window"
            title = "Strategic Example"
        }
    }
}
```

`maxViewportWidthRatio` 和 `maxViewportHeightRatio` 控制设计画布相对游戏客户区的最大占比；`cascadeOffsetX/Y` 控制多个同类启动窗口的初始级联偏移。它们均属于插件清单配置，不再由 Windows 宿主写死。

插件工厂只负责创建插件；控件的外观、位置、尺寸、条件和普通动作仍应放在 `.sgui/.sgfx` 中，避免重新把布局硬编码进 C++。

### 14.6 统一事件模型

`GuiCustomInputEvent::type` 可收到以下事件：

| 事件 | 含义 |
|---|---|
| `PointerMove` | 指针在控件内移动，或控件捕获鼠标后的移动。 |
| `PointerEnter` | 指针进入控件。 |
| `PointerLeave` | 指针离开控件。 |
| `PointerDown` | 鼠标按键按下；按键见 `event.button`。 |
| `PointerUp` | 鼠标按键释放。 |
| `PointerWheel` | 垂直或水平滚轮；增量见 `wheelDelta`，方向见 `horizontalWheel`。 |
| `KeyDown` | 获得焦点的控件收到按键按下；键值见 `keyCode`。 |
| `KeyUp` | 获得焦点的控件收到按键释放。 |
| `TextInput` | 文本字符输入；UTF-32 字符值见 `character`。 |
| `FocusGained` | 控件获得键盘焦点。 |
| `FocusLost` | 控件失去键盘焦点。 |
| `Cancel` | 宿主取消当前交互，控件应立即清除捕获相关状态。 |

鼠标按键类型为 `None`、`Left`、`Right`、`Middle`、`X1` 和 `X2`。`modifiers` 是位标志，可检测 Shift、Ctrl、Alt 及当前按下的各鼠标键；`repeatCount` 和 `repeated` 用于键盘重复，`horizontalWheel` 用于区分横向滚轮。

### 14.7 旧输入回调兼容

旧代码仍可填写 `handler.input`：

```cpp
handler.input = [](
    const gui::GuiResolvedWidget& widget,
    const gui::GuiCustomWidgetContext& context,
    gui::GuiCustomInputPhase phase,
    int mouseX,
    int mouseY
) -> bool {
    // 只兼容 Move、左键 Press、左键 Release。
    return false;
};
```

新控件应使用 `handler.event`。如果 `event` 和 `input` 同时存在，注册表优先调用 `event`，不会再调用旧 `input`。

### 14.8 D3D9 绘制安全规则

1. 不要对 `context.graphicsContext` 调用 `Release()`；设备所有权属于宿主。
2. 不要在 Handler 内调用 `BeginScene()`、`EndScene()`、`Present()` 或 `Reset()`。
3. 不要永久改变 Render Target、Depth Stencil、Viewport、Scissor、纹理或渲染状态；若必须改变，应完整保存并恢复。
4. 使用 `widget.rect` 作为最终绘制矩形，使用 `widget.opacity` 作为继承父级后的最终透明度。
5. `widget.hasClipRect` 为真时必须遵守 `widget.clipRect`；宿主会设置通用裁剪，但自建离屏表面或特殊绘制仍需自行保证不越界。
6. 不要在每帧 `draw` 中重复创建大型纹理、字体或顶点缓冲；把资源放入插件状态，在 `Initialize/Shutdown` 中管理生命周期。
7. `draw` 只负责绘制；游戏状态修改、Lua 动作和持久化应通过插件动作桥或数据提供器完成，不要在绘制回调中修改业务状态。

## 15. SGFX 资源类型

| 状态 | 资源语句 | 作用 |
|---|---|---|
| 现用 | `spriteTypes = { ... }` | 习惯性的资源容器；解释器会递归查找其中资源，本身没有运行时对象。 |
| 现用 | `spriteType = { ... }` | 普通图片资源。 |
| 现用 | `progressBarType = { ... }` | 进度条样式资源；类型名不区分大小写，因此 `progressbartype` 等价。 |
| 已实现未用 | `effectType = { ... }` | 安全内置 2D 效果资源；提供调色和周期脉冲，不加载任意 Shader。 |
| 现用 | `indexedMapResourceType = { ... }` | 索引地图资源及其离线生成参数、运行时着色参数。 |

**注意，当前本系统没有实现 `.sgfx` 的 `fontType`。字体由宿主直接扫描 `font` 目录，不需要也不能通过 `fontType` 注册。**

## 16. SGFX `spriteType`

```text
spriteType = {
    name = "GFX_example"
    texturefile = "gfx\\example\\image.png"
    noOfFrames = 8
    frameLayout = "horizontal"
    animation = {
        enabled = yes
        mode = "loop"
        fps = 12
        startFrame = 1
        endFrame = 8
    }
    loadType = "INGAME"
}
```

| 状态 | 语句 | 作用 |
|---|---|---|
| 现用 | `name = "GFX_name"` | Sprite 唯一资源名。 |
| 现用 | `texturefile = "path"` | 图片路径；以 `gfx/` 开头时相对项目根，否则相对项目的 `gfx` 目录。反斜杠会统一为斜杠。 |
| 已实现未用 | `textureFile = "path"` | `texturefile` 的大小写兼容形式；字段匹配本身不区分大小写。 |
| 仅解析 | `effectFile = "gfx\\FX\\...fx"` | 会保存，但当前自定义 D3D9 渲染器不加载 HOI3 Effect。 |
| 现用但仅解析 | `loadType = "INGAME"` | 当前文件已写入，字段会保存，但不会改变加载时机或资源生命周期。 |
| 现用 | `noOfFrames = 1` | Sprite Sheet 帧数，最小为 1；各帧等宽或等高切分。 |
| 已实现未用 | `frameLayout = "horizontal/vertical"` | 帧表排列方向；默认 `horizontal`，与原版常见横向 Sprite Sheet 一致。 |
| 已实现未用 | `animation = yes/no` | 简写：启用或关闭资源默认动画；启用时使用全部帧、`loop` 和默认每帧 100 ms。 |
| 已实现未用 | `animation = { ... }` | 声明资源默认动画；块一旦存在，`enabled` 默认 `yes`。 |
| 已实现未用 | `enabled = yes/no` | `animation` 块内是否默认自动播放。 |
| 已实现未用 | `mode = "loop/pingpong/once"` | `animation` 块内的播放模式。 |
| 已实现未用 | `frameTime = 100` / `frameDuration = 100` | 每帧毫秒数；二者互为别名。 |
| 已实现未用 | `fps = 12` | 用每秒帧数指定速度；与 `frameTime/frameDuration` 互斥。 |
| 已实现未用 | `startFrame = 1` / `endFrame = 8` | 资源默认动画范围；`endFrame = 0` 表示最后一帧。 |
| 已实现未用 | `offset = 50` | 资源动画时间偏移，单位毫秒；可为负数。 |
| 已实现未用但仅解析 | `norefcount = yes` | 会保存，但当前纹理缓存不使用该标志。 |

支持的实际图片格式由平台纹理加载器决定。当前 Windows 路径通过 GDI+/D3D9 处理项目已使用的 PNG、BMP、DDS 等资源。

## 17. SGFX 进度条与内置效果

### 17.1 `progressBarType`

```text
progressBarType = {
    name = "example_progress"
    size = { x = 270 y = 22 }
    horizontal = yes
    color = { 0.27 0.71 0.37 }
    colortwo = { 0.73 0.27 0.27 }
    textureFile1 = "gfx\\example\\progress_green.dds"
    textureFile2 = "gfx\\example\\progress_red.dds"
}
```

| 状态 | 语句 | 作用 |
|---|---|---|
| 现用 | `name = "resource_name"` | 进度条资源名。 |
| 已实现未用 | `size = { x = 270 y = 22 }` | 解析为资源建议尺寸；当前宿主实际使用 `.sgui` 控件的 `size`。 |
| 现用 | `horizontal = yes/no` | `yes` 横向填充，`no` 纵向填充。 |
| 现用 | `color = { r g b }` | `progressColor = 0` 使用的第一颜色。 |
| 现用 | `colortwo = { r g b }` | `progressColor = 1` 使用的第二颜色。 |
| 已实现未用 | `textureFile1 = "path"` | Windows D3D9 中作为 `progressColor = 0` 的填充贴图；按当前进度同步裁切。为空或加载失败时回退 `color`。 |
| 已实现未用 | `textureFile2 = "path"` | Windows D3D9 中作为 `progressColor = 1` 的填充贴图；为空或加载失败时回退 `colortwo`。 |
| 已实现未用但仅解析 | `effectFile = "path"` | 会保存，但当前通用进度条不执行 HOI3 Effect。 |
| 忽略 | `width = 270` | 当前 `china_anti_jap.sgfx` 中存在，但解析器不读取；应使用 `size = { x = 270 y = 22 }`。 |
| 忽略 | `height = 22` | 当前解析器不读取；应使用 `size`。 |

### 17.2 `effectType`

`effectType` 是本系统自己的安全效果抽象。它只计算 RGBA 乘数并交给统一 D3D9 2D 绘制链路，不读取 Sprite 的 `effectFile`，不加载原版 HOI3 `.fx`，也不会执行配置文件指定的任意 Shader。

```text
effectType = {
    name = "GFX_warning_pulse"
    effect = "opacity_pulse"
    color = { 1.0 0.8 0.8 1.0 }
    minimum = 0.35
    maximum = 1.0
    speed = 1.5
    phase = 0
    enabled = yes
}
```

| 状态 | 语句 | 作用 |
|---|---|---|
| 已实现未用 | `name = "GFX_effect"` | 效果资源唯一名称。 |
| 已实现未用 | `effect = "tint"` | 固定乘以 `color` 的 RGBA。 |
| 已实现未用 | `effect = "pulse"` / `"brightness_pulse"` | 在 `minimum..maximum` 间以正弦波改变 RGB 亮度，并乘以 `color`；两种名称等价。 |
| 已实现未用 | `effect = "opacity_pulse"` | 保持 `color.rgb` 调色，在 `minimum..maximum` 间周期改变 Alpha。 |
| 已实现未用 | `effect = "color_pulse"` | 在白色与 `color.rgb` 之间周期插值。 |
| 已实现未用 | `color = { r g b a }` | 效果颜色乘数，四个分量范围均为 `0~1`；默认全白。 |
| 已实现未用 | `minimum = 0.5` | 周期效果最小值，范围 `0~1`，不得大于 `maximum`。 |
| 已实现未用 | `maximum = 1.0` | 周期效果最大值，范围 `0~1`。 |
| 已实现未用 | `speed = 1.0` | 每秒周期数，范围 `0~100`；`0` 表示固定在由 `phase` 决定的位置。 |
| 已实现未用 | `phase = 0` | 初始相位，单位为度。 |
| 已实现未用 | `enabled = yes/no` | 关闭时返回全白乘数，相当于不应用效果。 |

控件通过以下字段使用效果：

| 状态 | 语句 | 作用 |
|---|---|---|
| 已实现未用 | `effectType = "GFX_effect"` | 固定引用效果资源；`effectResource` 是等价别名。 |
| 已实现未用 | `effectSource = "data.path"` | 动态读取效果资源名；列表模板支持 `item.field` 和 `{id}`，空值回退固定 `effectType`。 |
| 已实现未用 | `effectTimeSource = "state.time_ms"` | 使用数据值作为效果时钟，单位毫秒；未设置时使用窗口会话时钟。 |

效果可用于窗口框、图片、文字、按钮、色块、进度条、滚动条和索引地图。Custom 与 MarkerLayer 拥有自己的专用绘制器，不隐式套用内置效果。

## 18. SGFX `indexedMapResourceType`

### 18.1 运行时资源与样式

| 状态 | 语句 | 作用 |
|---|---|---|
| 现用 | `name = "GFX_map"` | 索引地图资源名。 |
| 现用 | `texturefile = "base.bmp"` | 地图底图。 |
| 现用 | `indexfile = "ids.bin"` | 每像素 `uint16` Region ID 图。ID 0 表示无 Region。 |
| 已实现未用 | `textureFile` / `indexFile` | 对应字段的大小写兼容形式。 |
| 现用 | `boundaryColor = { r g b a }` | 运行时 Region 边界 RGBA。 |
| 现用 | `hoverColor = { r g b a }` | 鼠标悬停 Region 的覆盖色。 |
| 现用 | `boundaryWidth = 1` | 边界扩张半径；最小可为 0。数值越大边界越粗。 |
| 现用 | `drawBoundaries = yes/no` | 是否生成并绘制边界层。 |
| 现用 | `colorStop = { ... }` | 添加一档数据颜色；可重复定义，加载后按阈值升序稳定排序。 |
| 现用 | `minimum = 20` | 该颜色档生效的最小值；运行时选取不大于当前值的最后一个 Color Stop。 |
| 已实现未用 | `threshold = 20` | `minimum` 的别名。 |
| 现用 | `color = { r g b a }` | 当前 Color Stop 的 RGBA。 |

### 18.2 离线地图生成字段

下列字段由 `gui_indexed_map_make` 使用，不是每帧运行时参数。

| 状态 | 语句 | 作用 |
|---|---|---|
| 现用 | `sourceDefinitionFile = "map\\definition.csv"` | 省份 ID 与地图颜色定义文件。 |
| 已实现未用 | `definitionFile` | `sourceDefinitionFile` 的别名。 |
| 现用 | `sourceProvinceFile = "map\\provinces.bmp"` | 原始 Province 颜色图。 |
| 已实现未用 | `provinceFile` | `sourceProvinceFile` 的别名。 |
| 现用 | `sourceGroupFile = "map\\region.txt"` | Province 到 Region 的分组定义。 |
| 已实现未用 | `groupFile` | `sourceGroupFile` 的别名。 |
| 现用 | `sourceItem = { id = 1 name = "region_name" }` | 把分组名映射到输出 ID；可重复。ID 必须为 `1..65535`，0 保留为空白。 |
| 现用 | `id = 1` | `sourceItem` 的输出 Region ID。 |
| 现用 | `name = "region_name"` | `sourceItem` 在分组文件中的名称。 |
| 现用 | `cropPadding = 16` | 对有效地图包围盒裁剪时保留的像素边距。 |
| 现用 | `flipVertical = yes/no` | 生成底图和 ID 图时同时进行垂直翻转。 |
| 现用 | `sourceFillColor = { r g b a }` | 生成底图时 Region 内部的默认颜色。 |
| 现用 | `sourceBoundaryColor = { r g b a }` | 生成底图时 Region 边界颜色。 |

## 19. 数据路径与列表模板约定

| 写法 | 作用 |
|---|---|
| `state.active` | 普通标量路径。数据键不区分大小写。 |
| `regions.{selectedregion.id}.name` | 使用另一个标量动态拼接路径。 |
| `regions.{id}.controlledPercentage` | 在索引地图中，`{id}` 代表当前 Region ID。 |
| `items.{id}.label` | 在列表实例中，`{id}` 代表列表项稳定 ID，而不是可变索引。 |
| `item.textkey` | 在列表模板或 Marker 中直接读取当前列表项字段。 |
| `spriteValuePrefix + 动态值` | 将紧凑数据值转换成完整 Sprite 名，例如 `1` 变成 `GFX_party_1`。 |

## 20. 使用本系统新增 GUI 的最小文件集合

### 20.1 纯 2D 静态或文件数据 GUI

1. `interface/<gui_name>.sgui`：窗口、控件、坐标、条件和动作名。
2. `interface/<gui_name>.sgfx`：图片、进度条\其他索引资源或索引地图资源。
3. `interface/gui_plugins/<gui_name>.txt`：注册插件、窗口名、启动方式、数据提供器和窗口层级。
4. `gfx/<gui_name>/...`：图片资源。
5. `font/<font_name>.ttf` 或 `.otf`：可选；`.sgui` 中使用文件名 stem。
6. `script_gui/data/<gui_name>.txt`：需要文件数据时添加。
7. `script_gui/<gui_name>.txt`：需要行为条件、Lua 回调映射或离线回退时添加。

### 20.2 实时游戏数据 GUI

在上面基础上还需要：

1. `script/<gui_module>.lua`：从 HOI3 接口建立快照并处理动作。
2. 在 `script/scripted_gui_plugins.lua` 注册插件 ID、频道、模块、刷新模式和发布优先级。

只要功能可由本文档中的内置控件和现有 Lua/HOI3 接口表达，就无需新增 C++。

## 21. 易误用字段

1. `fullScreen`：只绑定作为会话根窗口运行的 `windowType`；同一声明作为其他窗口的子控件时不会把父窗口替换为客户区。
2. `width`、`height`（SGFX 进度条资源）：当前完全忽略，改用 `size = { x = ... y = ... }`。
3. `positionType` 字符串引用的名称区分大小写；未找到资源时回退直接 `position`。`orientation` 未知值会按 `UPPER_LEFT` 布局并产生严格 Schema 诊断。
4. `scaleMode`：未知值会回退为 `stretch` 并写入加载诊断；九宫格启用时优先于 `scaleMode`。
5. `delayedTooltipText`：只是静态文本别名，不会自行产生延迟；必须另设 `tooltipDelay`。
6. `frame`：使用原版的 1 基编号，不是 0 基；`frameSource` 存在时会覆盖自动动画和静态 `frame`。
7. `animationTimeSource`：值的单位是毫秒，不是秒；资源与控件的 `offset` 会叠加。
8. `effectFile`、`loadType`、`norefcount`：Sprite 中仍只保存元数据，当前自定义渲染器不执行相应 HOI3 语义。
9. `textureFile1/2`（进度条）：是两套可选填充外观，不是背景与前景；背景仍应由独立 `iconType` 提供。
10. `renderMode = "custom"`：只抑制通用文字绘制，不会自动获得自定义绘制。
11. `customWidgetType`：必须有 C++ Handler，不能仅靠配置创造全新控件语义。
12. `scale` 是图片适配模式 `scaleMode` 的别名，不是几何缩放；几何缩放必须使用 `transformScale` 或动态 `scaleSource`。
13. 通用几何变换只作用于当前普通 2D 叶控件，不会连带变换其子控件；根窗口、列表、索引地图、MarkerLayer 和 Custom 不使用该变换。
14. `effectType` 是安全内置颜色效果资源；它与原版 Sprite 的 `effectFile` 无关，不能填写 `.fx` 路径或自定义 Shader 名。

## 22. SGUI 窗口静态数据

窗口可以直接声明只属于该界面的少量静态标量和静态列表。它们在每次数据刷新后由会话重新合并，因此不会被 Lua 的完整快照意外清除。可复用或便于扩展的业务目录更适合写入 `script_gui/data`，通过插件的 `base_data` 合并；动态游戏状态仍由数据提供器或 Lua 发布。

```gui
windowType = {
    name = "example_window"

    dataValueType = {
        name = "buttonsprites.CHI"
        value = "GFX_button_chi"
    }

    dataListType = {
        name = "officer_catalog"
        revision = 1

        item = {
            id = 1
            text = "OFFICER_LIST_ITEM_1"
            role = "military"
            portrait = "GFX_officer_1"
            namekey = "OFFICER_NAME_1"
            descriptionkey = "OFFICER_DESC_1"
            enabledwhen = "state.active"
        }
    }
}
```

| 状态 | 语句 | 作用 |
|---|---|---|
| 现用 | `dataValueType = { name = "path" value = "..." }` | 在当前窗口声明一个静态数据路径。布尔、整数、浮点和字符串会自动推断。 |
| 已实现未用 | `type = "string/bool/int/number"` | 强制指定 `dataValueType` 的值类型；类型和值不匹配时产生加载诊断。 |
| 现用 | `dataListType = { name = "list" ... }` | 在当前窗口声明静态列表。列表名进入统一 `GuiDataRegistry`。 |
| 现用 | `revision = 1` | 静态列表修订号，必须是非负整数。 |
| 现用 | `item = { id = 1 ... }` | 声明列表项；`id` 必须唯一且大于零，省略时按出现顺序自动分配。 |
| 现用 | `text = "KEY"` | 设置列表项主文本，同时保留为 `item.text` 字段。 |
| 现用 | 任意标量字段 | 以小写键保存为列表项数据，可通过 `item.field` 读取，也会进入动作参数。 |

### 22.1 Marker 静态目录补全

动态 Marker 列表只需要发布运行时字段；头像、姓名和说明等静态字段可从另一个列表按相同 `item.id` 补全：

```gui
markerLayerType = {
    name = "officer_markers"
    dataSource = "assigned_officers"
    catalogSource = "officer_catalog"
    regionSource = "item.regionid"
    portraitSource = "item.portrait"
    nameSource = "item.namekey"
    descriptionSource = "item.descriptionkey"
}
```

| 状态 | 语句 | 作用 |
|---|---|---|
| 现用 | `catalogSource = "list_name"` | 按 Marker 动态项的 `id` 查找静态目录项；动态项缺少字段时使用目录字段。 |
| 已实现未用 | `itemCatalog` / `fallbackDataSource` | `catalogSource` 的别名。 |

字段优先级为“动态项 > 静态目录项”。Marker 事件会先附加目录字段，再由动态字段覆盖，因此 Lua 动作既能收到静态身份参数，也能收到最新的 Region、坐标和顺序。

### 22.2 Bridge 基础数据合并

实时 Lua Bridge 可以在每个完整快照下方保留一份声明式基础数据。基础数据先加载，Lua 快照随后覆盖同名键；Lua 没有发布的静态列表不会因完整快照刷新而消失。

```gui
options = {
    inprocess_data_provider = "bridge"
    inprocess_channel = "lua"
    inprocess_bridge_name = "example_gui"
    inprocess_base_data = "script_gui/data/example_common.txt"
}
```

| 状态 | 语句 | 作用 |
|---|---|---|
| 现用 | `inprocess_base_data = "path"` | 在 Windows 进程内 Bridge 初始化时加载基础数据文件，并在每次 Lua 完整快照前重新作为底层数据。 |
| 已实现未用 | `base_data` / `base_path` | 直接创建 Bridge 数据提供器时使用的同义选项；插件清单的进程内选项需要添加 `inprocess_` 前缀。 |

战争地图主官目录当前保存在 `script_gui/data/china_anti_jap_common.txt`。每项通过 `tag = "CHI"` 等字段声明所属国家，`leader_candidate_list` 使用 `itemFilterField = "tag"` 和 `itemFilterValueSource = "state.viewertag"` 只显示当前玩家国家可用的主官。

## 23. 通用原生效果桥

原生效果桥用于让 Lua 在当前 HOI3 模拟线程中调用注入模块提供的游戏效果。普通数据写入直接调用原版 Effect、Command、Setter 或经过验证的字段与重算路径；只有显式选择 `event.*`、`decision.*` 时才进入原版事件/决议路径。它不在 D3D9 Draw/Input 回调中修改游戏状态，也不使用 GUI 渲染队列代替模拟线程。

### 23.1 Lua 接口

注入成功后，全局表 `NewCoreNative` 提供以下函数：

| 状态 | 语句 | 作用 |
|---|---|---|
| 现用 | `NewCoreNative.HasEffect("operation")` | 查询是否已有模块注册指定原生效果。 |
| 现用 | `NewCoreNative.ExecuteEffects(batch)` | 同步执行一个效果事务，返回 `success, code, message, transactionId`。 |

推荐通过 `script/native_effect_bridge.lua` 使用：

```lua
local NativeEffects = require("native_effect_bridge")

local success, code, message, transactionId =
	NativeEffects.ExecuteTransaction(
		"china_anti_jap",
		{
			{
				operation = "country.add_manpower",
				arguments = {
					tag = "CHI",
					amount = -10
				}
			},
			{
				operation = "province.add_modifier",
				arguments = {
					provinceIds = { 1234, 1235, 1236 },
					modifier = "warmap_emergency_mobilization",
					duration_days = 60
				}
			}
		},
		true
	)
```

`atomic = true` 时，整个批次会先完成准备和校验。任一效果执行失败后，已经执行的效果按相反顺序回滚。包含多个效果的原子事务要求每个 Handler 都提供回滚函数。

### 23.2 Batch 字段

| 字段 | 类型 | 作用 |
|---|---|---|
| `source` | string | 调用来源，例如 `china_anti_jap`。 |
| `atomic` | bool | 是否要求事务化执行；默认 `true`。 |
| `effects` | array | 按顺序执行的原生效果列表，最多 256 项。 |
| `operation` / `effect` / `type` | string | Handler 注册的效果名称，三者为同义字段。 |
| `arguments` | table | 传给 Handler 的参数；也可以把参数直接写在效果表中。 |

参数支持布尔值、整数、浮点数、字符串和由这些标量组成的数组。参数名和效果名统一按小写处理。

### 23.3 执行规则

- 调用发生在 Lua C 函数返回之前，因此成功返回时效果已经生效。
- 非战局状态或 Native SaveLoaded 屏障关闭时拒绝执行；服务在 prepare 前取得真实屏障写许可租约，并持有到 apply/rollback 完成。
- 每个游戏生命周期代际只允许一个 HOI3 模拟线程执行原生效果。
- 屏障闭锁、进入/退出战局、切换玩家国家或收到 `SaveLoaded` 时重置线程绑定；疑似读档期间不会执行延迟队列。
- GUI 只负责产生动作；不得在 D3D9 Draw/Input 回调中直接修改游戏数据。
- 具体 `operation` 必须由注入模块注册，解释器不会硬编码战争地图字段。

### 23.4 C++ Handler 注册

模块可通过 `core::Services::effects` 注册任意新机制：

```cpp
services.effects->RegisterHandler(
	"country.add_manpower",
	[](const core::NativeEffect& effect,
	   const core::NativeEffectExecutionContext& context,
	   core::PreparedNativeEffect& prepared,
	   std::string& error)
	{
		// 在这里解析参数并取得稳定的 HOI3 对象。
		prepared.apply = [](std::string& applyError)
		{
			// 在当前模拟线程中修改游戏状态。
			return true;
		};
		prepared.rollback = []
		{
			// 恢复修改前的状态。
		};
		return true;
	},
	error
);
```

战争地图省份任务已经使用 `country.add_manpower`、`province.add_modifier` 和 `province.remove_modifier` 建立原生即时效果闭环。任务按钮、任务条件、国家白名单、消耗、持续时间、Modifier 和 Region 到 Province 的展开规则仍保存在 SGUI、行为文件、静态数据与 Lua 适配器中，C++ Handler 不包含战争地图业务字段。

### 23.5 当前注册的 59 个 HOI3 操作

| 数据域 | 已注册操作 | 主要参数与说明 |
|---|---|---|
| 全局 Flag | `global.set_flag`、`global.clear_flag` | `name`，兼容 `flag`、`global_flag`；调用原版 Effect 并读回验证，可回滚。 |
| 事件与决议 | `event.fire`、`event.execute`、`event.enqueue`、`event.cancel`、`decision.execute`、`decision.enqueue`、`decision.cancel`、`queue.cancel` | 事件用 `id`，决议用 `name`；可选 `tag`、`province_id`、`delay_days`；取消使用 `transaction_id` 或内部 `queue_id`。决议仅允许当前玩家国家。 |
| 省份归属与建筑 | `province.set_owner`、`province.set_controller`、`province.add_core`、`province.remove_core`、`province.set_building_level` | 使用 `province_id`；归属/核心另用目标 `tag`，建筑另用建筑定义名和 `level`。 |
| 科技、研究与首都 | `technology.set_level`、`research.set_progress`、`research.complete`、`research.cancel`、`country.set_capital`、`country.set_acting_capital` | 科技/研究使用定义名；首都使用 `province_id`。这些国家操作限制为当前玩家国家。 |
| 国家经济与池值 | `country.add/set_manpower`、`country.add/set_goods`、`country.add/set_national_unity`、`country.add/set_dissent`、`country.add/set_neutrality`、`country.add/set_officers`、`country.add/set_diplomatic_influence`、`country.add/set_leadership`、`country.add/set_convoys`、`country.add/set_escorts`、`country.add/set_free_spies` | `add_*` 使用 `amount`/`delta`，`set_*` 使用 `value`/`target`；默认目标为当前玩家，显式 `tag` 也必须等于当前玩家。Goods 支持 `supplies`、`fuel`、`money`、`crude_oil`、`metal`、`energy`、`rare_materials`。 |
| 政府与意识形态 | `country.set_government`、`country.set_ruling_ideology`、`country.add/set_ideology_popularity`、`country.add/set_ideology_organization` | 使用政府/意识形态定义名；政府 Setter 后执行当前已恢复的政治状态重新校验。 |
| 外交数值 | `diplomacy.add_relation`、`diplomacy.set_relation`、`diplomacy.add_threat`、`diplomacy.set_threat` | `tag` 为当前玩家，`target_tag` 为另一国家，数值用 `amount` 或 `value`。 |
| 间谍与情报 | `espionage.set_presence_level`、`intelligence.set_province_level` | 前者使用 `target_tag` 和 `level`，后者使用 `province_id` 和 `level`。 |
| Modifier | `country.add_modifier`、`country.remove_modifier`、`province.add_modifier`、`province.remove_modifier` | 使用定义名 `modifier`；添加支持 `duration_days`，省份操作使用 `province_id`。 |

> 表中的 `country.add/set_x` 是成对操作的缩写，不是字面 operation 名。例如应写 `country.add_manpower` 或 `country.set_manpower`。

注入层事件/决议队列最多 1024 项，`delay_days` 范围为 0～365000；延迟 0 也要等下一次核心 Tick。该队列不进入 HOI3 存档。精确原生读档 Hook 会在保存文件反序列化开始前闭锁屏障并清空队列，稳定恢复后发布 `SaveLoaded`；对象指纹、不可读窗口和日期回退检测继续作为回退保护。

### 23.6 通用原生查询与能力发现

`NewCoreNative` 同时提供只读查询和能力发现：

| 状态 | 语句 | 作用 |
|---|---|---|
| 现用 | `NewCoreNative.HasQuery("operation")` | 只判断当前进程是否注册了查询 Handler；不替代版本/符号可用性检查。 |
| 现用 | `NewCoreNative.Query("operation", arguments)` | 同步执行只读查询，返回 `success, value, code, message`。失败时 `value` 为 `nil`。 |
| 现用 | `NewCoreNative.GetCapability("query.operation")` | 返回能力快照；不存在时返回 `nil`。同样可查询 `resolver.*`、`effect.*`、`engine.symbol.*`、`engine.type.*`、`engine.field.*`。 |

Capability 快照字段：`id`、`provider`、`kind`、`access`、`availability`、`available`、`reason`、`rollback`、`persistence`、`multiplayer`，以及可选 `versionId`。`HasQuery`/`HasEffect` 表示“名称已注册”，`capability.available` 才表示其版本与依赖在当前进程中可用。

```lua
local capability = NewCoreNative.GetCapability(
	"query.country.manpower"
)

if capability and capability.available then
	local ok, manpower, code, message = NewCoreNative.Query(
		"country.manpower",
		{ tag = "CHI" }
	)
	if ok then
		-- manpower 是只读数值；不得把原生地址传回 Lua。
	end
end
```

当前查询参数支持 `nil`、布尔、整数、浮点数、字符串、数组和对象，最大递归深度为 8；顶层参数最多 256 项。执行必须处于 Gameplay、当前 HOI3 Lua/模拟线程和开放的 SaveLoaded 稳定读屏障中。服务内部只使用稳定 Tag、Province ID 等重解析对象，不缓存跨代际裸地址。

# 3. 将领俘虏系统机制总结与源码分析

## 3.1 模块边界

将领俘虏系统已经完全迁入 New Core：

| 文件 | 责任 |
|---|---|
| `new_core/src/leader_capture/leader_capture_core_module.h/.cpp` | `core::IModule` 适配层；向统一 Hook 注册器注册 `hoi3.leader_capture`，接收生命周期事件并执行 100 ms Tick。 |
| `new_core/src/leader_capture/leader_capture_engine.h/.cpp` | 固定版本签名校验、补丁安装/恢复、战斗上下文、俘获国判定、将领池操作、Held 队列、转移监视和日志。 |
| `new_core/tests/leader_capture_core_module_probe.cpp` | 验证模块 ID、优先级、Hook 注册、生命周期门控和清理行为。 |

它没有独立 `DllMain`、独立注入器、独立 DLL 或私有 Worker。安装、Tick 和关闭全部由 New Core 统一调度。

## 3.2 当前产品规则

当前规则不是概率系统，而是：

> 只要能够证明单位是由战斗永久歼灭、单位确有将领、败方与胜方归属上下文完整，并且原版移除后的对象状态满足安全前置条件，就捕获该将领并自动转移给可靠解析出的俘获国。

明确不处理：

- 玩家或 AI 手动解散单位；
- 非已知战斗移除调用点；
- 没有将领的单位；
- 无法建立败方单位与胜方集合对应关系的移除；
- 联军战斗中无法可靠确定俘获国的情况；
- 重复捕获、队列已满、原版移除后对象状态不一致的情况。

`EvaluateCaptureEligibility`、`ResolveCaptor` 和 `EvaluateTransferPolicy` 是未来加入概率、技能、特性、战争类型或国家规则的策略缝。目前实现刻意保持最小，不在 Hook 现场混入复杂玩法政策。

## 3.3 Hook 与原版调用点

当前补丁组安装 4 个 CALL 替换和 1 个 Inline Hook：

| RVA | 类型 | 用途 |
|---:|---|---|
| `0x0016F203` | CALL Hook | 战斗结果路径 A，调用原版 `0x00174580` 前记录败方单位、胜方国家集合与省份。 |
| `0x0016F247` | CALL Hook | 战斗结果路径 B，作用同上，但双方槽位相反。 |
| `0x001CA7CB` | CALL Hook | 精确标记撤退/延迟销毁生产者，避免把无关延迟移除误判为歼灭。 |
| `0x00165C87` | CALL Hook | Combat A 在战斗结果上下文消失前预先恢复对手集合，修复先移除、后结果的竞态。 |
| `0x00284E40` | Inline Hook | 共享单位移除函数；按返回地址区分手动解散、Combat A 和 Combat B。 |

原版将领池函数：

- `0x000DDB80`：向国家将领注册表添加将领；
- `0x000DDCA0`：从国家将领注册表移除将领。

安装前 `ValidateAll()` 会验证原 CALL 目标、共享函数序言、手动解散/两条战斗调用点签名和将领池函数签名。任一项失败，整个 `hoi3.leader_capture` 补丁组拒绝安装；部分安装失败时会恢复已经写入的补丁。

## 3.4 关键对象布局

当前引擎直接使用的主要已验证字段：

| 对象 | 偏移 | 含义 |
|---|---:|---|
| Unit | `+0x10/+0x14` | 稳定单位双 ID，用于关联战斗结果与移除调用。 |
| Unit | `+0x128` | 所属国家内部 ID。 |
| Unit | `+0x12C` | 当前将领指针。 |
| Leader | `+0x40` | 反向 Unit 指针；原版移除后必须变成空。 |
| Country | `+0xCA4` | 三字符国家 Tag。 |
| Country | `+0xE00` | 将领注册表。 |
| Combatant | `+0x3C` | 所属 Combat。 |
| Combat | `+0x10/+0x14` | 双方 Combat Side。 |
| Combat | `+0x18` | 战斗省份。 |

这些地址只在 `leader_capture_engine.cpp` 内部使用，不向 Lua、GUI 或模块 ABI 暴露裸指针。

## 3.5 执行流程

1. **建立归属上下文**：战斗结果 Hook 遍历败方单位，记录单位双 ID、战斗省份、当前控制者和最多 16 个胜方国家；Combat A 的预播种 Hook 处理原版调用顺序竞态。
2. **识别移除原因**：共享移除 Hook 检查返回地址。手动解散 `0x001DA10A` 直接调用原版并返回；只有两条已确认的战斗路径继续处理。
3. **验证前置条件**：上下文必须存在，将领反向指针必须仍指向该单位，来源国家和 Tag 可读，原调用参数必须为已验证组合，且将领未重复进入 Held 队列。
4. **先执行原版逻辑**：始终先调用原版共享移除函数，避免破坏单位销毁本身。
5. **验证原版后状态**：将领反向 Unit 指针必须已经清空，但将领仍位于败方将领池；否则拒绝捕获。
6. **进入 Held**：从败方将领池移除将领并提交 Held 队列；若队列提交失败，立即尝试加回来源国家。
7. **解析俘获国**：单一胜国直接解析；联军只接受“省份控制者属于胜方集合”的交集结果。
8. **自动转移**：调用原版国家将领添加函数，把将领加入俘获国；随后验证来源池不存在、目标池存在。
9. **转移监视**：最多监视 5 分钟，记录原版是否发生意外回填或回退，但监视本身不再次写入。

## 3.6 容量、生命周期和诊断

| 项目 | 当前值 |
|---|---:|
| 战斗上下文容量 | 512 |
| Held 将领容量 | 256 |
| 转移监视容量 | 256 |
| 单场记录胜方国家上限 | 16 |
| 战斗上下文 TTL | 20 分钟 |
| 转移监视 TTL | 5 分钟 |
| 控制者轮询周期 | 100 ms |

生命周期进入 Frontend、玩家变化、显式 SaveLoaded 或运行时停止时会关闭 Gameplay 写入并清空临时状态。`F6` 是开发期安全释放键：把最早的 Held 将领放回来源国家；`F8` 输出完整状态。日志明确提示 Held 非空时不要保存，因为 Held 队列当前不进入 HOI3 存档。

## 3.7 已知边界

- 这是一个专用战斗 Hook，不等于通用 `leader.*` 数据接口；统一原生效果注册器目前没有 `leader.transfer_pool`、`unit.assign_leader` 等 Handler。
- Hook RVA、返回点、签名、预期 CALL 目标和相关 Unit/Leader/Combat/Country 字段均已迁入统一 Version/Symbol/Type Registry；模块仍是 D328 专用策略实现，但不再拥有版本地址真值。
- 联军无法可靠归属时只 Hold，不按参战比例、最高指挥官或伤害贡献猜测。
- 当前没有俘获概率、将领技能/特性筛选、处决、释放、交换、战俘营或外交谈判机制。
- Held 状态未序列化；必须补齐同 Tag 读档检测和持久化策略后，才能允许 Held 跨存档存在。
- 多人同步和确定性尚未验证，默认只按单机扩展机制看待。

# 4. 通用注册器：已注册与未注册的数据 Hook

## 4.1 术语说明

本工程有两类容易混淆的“Hook”：

1. **控制流 Hook**：由 `HookRegistry` 管理，修改 D3D9、Lua 或 HOI3 机器码调用路径。当前只有 `windows.d3d9`、`windows.lua51`、`hoi3.leader_capture` 三组。
2. **数据操作 Handler**：由 `NativeEffectService` 按 operation 名注册。它们不一定安装机器码 Hook，而是在 HOI3 Lua/模拟线程中调用已经逆向出的原版函数、Command、Effect、Setter 或受控字段写入。当前有 59 个。

后文把用户侧常说的“数据 Hook”统一写成“原生效果 Handler”，避免误判工程状态。

## 4.2 当前 3 组控制流 Hook

### `windows.d3d9`

- Hook `IDirect3D9::CreateDevice`；
- Hook 实际设备 `Reset`、`Present`、`EndScene`；
- Hook `IDirect3DSwapChain9::Present` 作为兼容绘制路径；
- 子类化游戏窗口 WndProc，把鼠标移动、按下、释放、滚轮等消息送入统一 GUI 输入模型；
- 维护 VTable 所有权，设备 Reset 前释放资源，Reset 后重建。

### `windows.lua51`

- 捕获 HOI3 创建/关闭的 Lua 5.1 State；
- 注册 `NewCoreNative`、GUI 数据发布、动作拉取和生命周期相关原生函数；
- 管理 State 所有权，避免旧 State、错误线程或退出战局后的调用继续写入。

### `hoi3.leader_capture`

- 4 个已验证 CALL 点 + 1 个共享移除函数 Inline Hook；
- 统一安装、签名验证、部分失败回滚和逆序卸载；
- Gameplay 生命周期门控，Frontend 不进行将领池写入。

## 4.3 当前 59 个原生效果 Handler

下面是源码注册表的完整字面操作名：

### 全局、事件与队列（10）

```text
global.set_flag
global.clear_flag
event.fire
event.execute
event.enqueue
event.cancel
decision.execute
decision.enqueue
decision.cancel
queue.cancel
```

### 省份、科技、研究与首都（11）

```text
province.set_owner
province.set_controller
province.add_core
province.remove_core
province.set_building_level
technology.set_level
research.set_progress
research.complete
research.cancel
country.set_capital
country.set_acting_capital
```

### 国家经济、库存与池值（22）

```text
country.add_manpower
country.set_manpower
country.add_goods
country.set_goods
country.add_national_unity
country.set_national_unity
country.add_dissent
country.set_dissent
country.add_neutrality
country.set_neutrality
country.add_officers
country.set_officers
country.add_diplomatic_influence
country.set_diplomatic_influence
country.add_leadership
country.set_leadership
country.add_convoys
country.set_convoys
country.add_escorts
country.set_escorts
country.add_free_spies
country.set_free_spies
```

其中 `country.add/set_leadership` 不是简单改显示缓存：当前实现选择首都或显式 `source_province_id` 的省份领导力来源，通过原版省份 Setter 求得目标总领导力，并验证国家总量。该接口比普通池值更依赖省份来源模型。

### 政府与意识形态（6）

```text
country.set_government
country.set_ruling_ideology
country.add_ideology_popularity
country.set_ideology_popularity
country.add_ideology_organization
country.set_ideology_organization
```

### 外交、间谍与情报（6）

```text
diplomacy.add_relation
diplomacy.set_relation
diplomacy.add_threat
diplomacy.set_threat
espionage.set_presence_level
intelligence.set_province_level
```

### 国家与省份 Modifier（4）

```text
country.add_modifier
country.remove_modifier
province.add_modifier
province.remove_modifier
```

## 4.4 已有 Lua 正常命令，原则上不重复注入的能力

`LUAClassReference.wiki` 和仓库脚本表明，下列操作已有正常 Lua Command/Action 路径。除非以后明确需要绕过规则的 `force` 语义，否则应继续走 Lua，而不是再造直接字段 Setter：

- 国家 flag、变量、法律、部长、动员、傀儡释放；
- 生产队列、建筑排队、单位排队、生产分配、补给生产；
- 开始研究、领导力分配比例、间谍任务与优先级；
- 正常宣战、结盟、阵营邀请、军通、互不侵犯、保证独立、影响国家、制裁、债务、远征军、贸易等外交 Action；
- `CAIStrategy` 的备战、取消备战和部分 AI 计划接口。

这些 Lua Action 会经过原版合法性、接受度、规则和对方响应。它们与“无条件立即强制修改”不是同一语义。

## 4.5 尚未注册的通用数据能力

以下名称是建议接口族或逆向方向，不是当前可调用 Handler。

### 运行时、存档与全局控制

- `runtime.set_paused`、`runtime.set_speed`：按钮和 UI 路径已找到，但权威模拟控制函数尚未恢复；
- `debug.set_date`：原版控制台日期字段写入已定位，但跳日/回拨无法回滚期间日结算，只应是隔离的调试接口；
- `player.set_controlled_country`：不能只写玩家 Tag/索引，必须同步 AI 接管、迷雾、输入权限、消息和多人状态；
- `save.request`、`save.load`、`scene.change`：需要异步服务、完成回调和全局对象失效屏障；
- `rng.get_state/set_state`：尚未恢复稳定 RNG 对象、状态大小、种子和序列化位置；
- 保存文件异步请求、主动加载和场景切换控制服务尚未实现；精确同玩家 Tag 的 SaveLoaded 开始/完成 Hook 已完成并注册。

### 国家政治与外交强制状态

- `country.set_surrender_progress`：投降度是领土/胜利点等来源的派生值，正式机制不应只写显示缓存；
- `country.set_alignment`：阵营轴坐标暂缓，尚未完成漂移、阵营和外交缓存同步；
- `war.force_declare/peace/armistice`；
- `faction.force_join/leave`；
- 强制傀儡/宗主、军事通行、互不侵犯、保证独立、禁运、贸易路线与远征军控制权；
- 外交距离、阵营进度及其他尚未注册的关系派生状态。

### 省份与地图

- 省份原始人力、资源、领导力来源、胜利点、地形和天气；
- 补给节点、补给来源、吞吐、路径、拥堵和库存；
- Region 级原生对象不存在；`region.*` 应由 Lua/数据层展开为多个 `province.*` 操作，而不是伪造 HOI3 Region 指针；
- 建筑在建进度、损坏程度和修复队列尚未形成通用接口，当前只有已完成等级 `province.set_building_level`。

### 单位、编制与通用将领接口

- `unit.spawn/destroy/transfer`；
- `unit.set_strength/organisation/experience/supply/fuel`；
- `unit.set_location/issue_order`；
- `unit.attach_subunit/detach_subunit`；
- `unit.assign_leader`；
- `leader.transfer_pool/set_status/set_rank/set_skill/set_experience/set_traits`；
- 指挥链、战区、集团军群、集团军、军、师的通用增删与重挂接；
- 远征军所有权/控制权的即时转移。

`leader_capture` 已恢复一条专用将领池转移路径，但没有把它注册为通用 `leader.*` API。

### 战斗、补给与 AI

- 战斗对象、双方、进度、宽度、预备队、伤亡、撤退和增援；
- 陆海空任务、路径规划、战术目标和命令队列；
- 补给图节点、路径重算、吞吐和运输网络；
- 战区划分、前线、进攻/防御兵力评分、投入/撤退判断；
- “兵力劣势仍进攻”等 AI 战术评分 Hook 和参数接口。

### 科技与其他高级能力

- 蓝图、许可证和科技派生单位属性的完整通用重算服务；
- 3D 模型、原版 Effect/Shader、地图 3D 对象和相机控制；
- 音频/音乐播放控制；
- 多人同步命令、主机权限和确定性回放。

## 4.6 原生效果的强制工程约束

每个新增 Handler 必须满足：

1. 绑定明确的可执行文件版本或签名 Profile；
2. 只在 Gameplay 和正确 HOI3 模拟线程执行；
3. Lua 只传 Tag、Province ID、Unit ID 等稳定标识，不传裸指针；
4. 参数、范围、对象存在性和定义名称必须先验证；
5. 优先调用原版 Command/Effect/Setter，字段写入必须有读回和重算依据；
6. 原子批次中的 Handler 必须提供真正可用的 rollback；
7. 读档、退出战局和玩家变化后不能保留旧对象地址；
8. 必须验证即时读回、次日稳定、存档往返和错误输入；
9. 诊断记录稳定 ID、operation、事务 ID 和错误码，不长期记录可复用裸地址；
10. 多人模式未完成同步前，所有直接内存写入按单机能力处理。

# 5. HOI3 当前逆向进度

## 5.1 已建立的逆向锚点

当前逆向不是“恢复完整源码”，而是围绕可验收产品 A——HOI3 原生扩展核心——建立稳定接口。主要锚点包括：

- `LUAClassReference.wiki`：124 类/类型规模的 Lua 对象、Getter、Command 和 Action 目录；
- PE 时间戳、映像大小和 32 位调用约定；
- `GameState`、国家数据库、定义注册表、省份容器等全局对象；
- 当前玩家国家、日期、国家经济字段、政治字段、关系表、间谍表、研究表、建筑表、Modifier 列表等对象布局；
- 原版 Manpower/Goods/Modifier/政治/威胁/核心/控制者/首都/科技/研究/事件/决议等 Effect、Command 与 Setter；
- D3D9 创建与呈现链、游戏 WndProc、Lua 5.1 State 创建链；
- 两条战斗永久移除路径、战斗结果双方结构、将领与单位双向关系、国家将领池函数；
- `GetUnitsIterator` 的原生绑定证明国家单位链头/尾/计数位于 `Country+0xBAC/+0xBB0/+0xBB4`，节点保存 Unit 指针及前后链；
- Leader 通用对象构造和去重路径证明内在对象 ID 位于 `Leader+0x08/+0x0C` 且必须成对比较；原生存档写入器在 Leader 类型上下文中把第二项写为 `active_leaders` 键。现役与预备将领表根分别位于 `Country+0xE00` 和 `Country+0xE10`。

所有已落地游戏数据操作都由 `NativeEffectService` 提供统一事务边界。版本识别、原生函数与全局对象 RVA、对象字段布局、命令 ABI、将领捕获 Hook 签名和 D3D9 帧探针调用点均已集中到 Engine Version/Symbol/Type Registry；业务模块只消费语义化注册项。

## 5.2 分系统完成度

| 系统 | 当前状态 | 说明 |
|---|---|---|
| 启动、注入与握手 | **已形成闭环** | 双模式启动器、挂起创建、DLL 注入、共享内存握手、模块/Hook 状态和错误报告已完成。 |
| 模块/Hook/生命周期基础设施 | **安全屏障与精确读档信号已完成** | 模块与 Hook 注册、Tick、逆序关闭、精确同 Tag 读档开始/完成、观察式回退、三次稳定恢复和写入硬门控均已完成。 |
| Reverse Probe Framework | **只读实机闭环已形成** | 统一探针注册、选择性执行、Symbol/Type/Field/Capability 前置条件、异常隔离、证据等级、JSONL 报告、稳定只读租约和 SaveLoaded 写探针硬门控已完成；Query 与 Unit/Leader 代际探针已注册并可由 Lua 请求触发。 |
| Script GUI 2D 系统 | **达到当前 2D 验收标准** | 声明式布局、资源、文字、按钮、列表、滚动条、进度条、Tooltip、索引地图、Marker、Custom、动画、变换、Lua 双向通信、多窗口和持久化已完成。 |
| Script GUI 3D/原版 Shader | **未完成** | 无通用 3D 模型控件，不加载任意 HOI3 `.fx`。 |
| Native Effect 事务层 | **已完成基础闭环** | Handler 注册、prepare/apply/rollback、线程和生命周期约束、Lua API 已完成。 |
| 国家经济与政治写入 | **大范围已落地** | 人力、物资、凝聚力、不满、中立、军官池、外交点、领导力、运输船、护航、间谍池、政府与意识形态等已注册。 |
| 省份、建筑、科技与研究 | **关键写入已落地** | Owner/Controller/Core、已完成建筑等级、科技等级、研究进度/完成/取消、正式/临时首都已注册。 |
| 外交与情报 | **数值层部分完成** | 关系、威胁、间谍 Presence、省份 Intel 已完成；战争、和平、阵营与条约强制状态未完成。 |
| 全局 Flag 与事件/决议 | **已落地** | Flag、事件/决议立即执行、注入层按日队列和取消已完成；队列不序列化。 |
| 将领俘虏 | **专用机制已落地** | 战斗歼灭捕获和自动转移已完成；不是通用单位/将领 SDK。 |
| 通用只读 Query Service | **同代际批量闭环已完成** | 已建立统一 Query Registry、Capability 依赖、稳定对象重解析、SaveLoaded 稳定租约、单项/批量 Lua API；当前 18 个查询覆盖 Country、Province、GameState、Technology、Relation、Unit 和 Leader，批量快照在代际变化时拒绝暴露部分结果。 |
| 单位与编制 | **只读对象层已形成第一版闭环** | Unit 从全局国家表下的国家单位链按原生双 ID 重解析，覆盖无将领单位；Leader 从现役/预备表按原生对象 ID 对重解析，覆盖未任命将领。尚未形成编制树、单位属性和命令写入 SDK。 |
| 战斗系统 | **仅局部逆向** | 已恢复将领捕获所需战斗双方、败方单位和移除路径；没有通用战斗 Query/Effect。 |
| 补给网络 | **未形成接口** | 节点、路径、吞吐和重算尚未系统逆向。 |
| AI 战术 | **未形成 Hook SDK** | Lua 战略准备可用，但底层战术评分、路径、投入和撤退仍未注册。 |
| 存档、场景、玩家、RNG | **精确读档屏障已完成，控制服务未完成** | 同 Tag 读档开始/完成 Hook 与 fail-closed 写入屏障已完成；异步保存/读取、场景切换、控制国切换和 RNG 状态接口未完成。 |
| 多人确定性 | **未验收** | 当前直接写入能力只能按单机机制发布。 |

## 5.3 按逆向验收等级评估

沿用《HOI3完整逆向工程可行性与最小验收系统备忘录》的分级：

| 等级 | 当前判断 |
|---|---|
| R0：可识别与可注入 | **完成**。固定版本识别、注入、握手、模块和 Hook 状态可观测。 |
| R1：生命周期与稳定对象 | **九类稳定对象已纳入统一层**。Frontend/Gameplay/玩家变化、精确同 Tag 读档 Hook、观察式回退、Native SaveLoaded Barrier、generation 失效，以及 GameState、CountryDatabase、Country、Province、TechnologyDefinition、TechnologyStatus、Relation、Unit、Leader 重解析已完成；Combat 与 Supply 仍待推进。 |
| R2：通用读写 API | **第一版双向接口层形成**。已有 59 个写操作、18 个只读查询、Capability Registry 和 Version/Symbol/Type Registry；Effect 中 Country、Province、TechnologyDefinition、Relation 的局部解析已迁移到统一 Resolver。 |
| R3：主要玩法子系统 | **部分完成**。经济、政治、省份、科技、Modifier、事件等关键写入存在；生产、外交强制状态、单位系统不完整。 |
| R4：战斗、补给与战术 AI | **只完成专用切片**。将领捕获证明可以介入战斗生命周期，但通用战斗、补给和 AI SDK 尚未开始闭环。 |
| R5：原版 GUI、地图与 3D | **自研 2D Overlay 完成，原版 3D 未完成**。不能等同于完全恢复 HOI3 GUI/渲染内核。 |
| R6：存档、确定性与多人 | **未完成**。GUI 有自身持久化，不能代表原生对象、队列和多人同步已经解决。 |
| R7：行为兼容重实现资料 | **远未完成**。当前工程是扩展核心，不是 HOI3 完整兼容重实现。 |

因此，当前已经超过“单一 GUI 注入 Demo”，进入了 **可用的单版本原生扩展核心** 阶段；Version/Symbol/Type Registry、ReverseProbeFramework、精确原生读档 Hook、Native SaveLoaded Barrier、Capability Registry、Stable Object Resolver 和 Native Query Service 已形成第一版长期工程基础。Technology、Relation、Unit 和 Leader 已进入统一对象层，Effect 的 Country、Province、TechnologyDefinition、Relation 查找也已迁移；下一阶段重点转为 Combat/Supply，而不是继续堆叠零散 Setter。

## 5.4 当前最重要的基础设施缺口

Version Profile、Symbol Registry、Type Registry、逐符号验证/失效、ReverseProbeFramework、Native SaveLoaded Barrier、Capability Registry、Stable Object Resolver 和 Native Query Service 已经落地第一版。剩余缺口是：

1. **Combat/Supply Resolver 与 Query**：应先以只读方式定义稳定键、对象边界和代际规则，再评估可写能力。
2. **Unit/Leader 高层对象模型**：底层稳定重解析已完成；仍需定义编制树、单位属性、将领详情、批量枚举和受控命令接口。
3. **Capability 元数据精化**：Query 和 Resolver 已声明具体符号/类型/字段依赖；现有 Effect 仍需逐操作补齐精确依赖、真实 rollback/persistence/multiplayer 分类，并纳入模块/Hook 动态状态。
4. **Probe 覆盖继续扩展**：同代际 Query 与 Unit/Leader 代际探针已经迁入框架；仍需为 Technology、Combat、Supply 等新增对象补充注册式实机探针，并长期保存失败隔离报告。

## 5.5 推荐后续顺序

1. 为 Technology 补充具名定义与状态对象的注册式实机探针；
2. 以只读方式逆向 Combat 与 Supply Network，纳入统一 Resolver/Query/ReverseProbe 后再决定哪些写入可以安全开放；
3. 扩展 Unit/Leader 批量枚举、编制树和详情 Query；只缓存值快照，不缓存跨 SaveLoaded 的原生地址；
4. 为 59 个 Effect 补齐逐操作 Capability 依赖、回滚、持久化和多人元数据，并把模块/Hook 动态状态纳入 Registry；
5. 实现暂停/速度的 `RuntimeControlService`，不要把会话控制伪装成普通原子效果；
6. 最后进入 AI 战术 Hook、3D、存档异步服务和多人确定性。

## 5.6 当前验收结论

New Core 当前可以被验收为：

> 面向单一已验证 HOI3 TFH 4.02 32 位可执行文件，能够稳定注入、统一管理模块与 Hook、在游戏内运行声明式 2D GUI、通过 Lua 双向桥发布数据和动作、执行 59 个受控原生效果、18 个统一原生查询及同代际批量快照、按统一安全策略运行可审计逆向探针，并承载一个已投入实机测试的战斗将领俘虏机制的扩展核心。

它目前不能被验收为：完整 HOI3 逆向 SDK、通用单位/战斗/补给 API、完整 3D GUI 引擎、原生存档服务、多人安全修改层或 HOI3 行为兼容重实现。

# 6. 从 New Core 到独立 HOI3 兼容重实现引擎

## 6.1 总体判断

将 New Core 继续发展为一个不依赖 `hoi3_tfh.exe`、能够读取 HOI3/TFH 原始数据并独立运行战局的开源兼容重实现引擎，在技术上是可行的。当前工程已经解决了最困难的前置问题之一：我们拥有一个可以进入原版进程、识别版本、观察对象、执行受控读写、记录跨读档代际并验证行为的 **原版运行时 Oracle（行为参照与差分观测平台）**。

但是，这一目标不是“把 DLL 改成 EXE”或“逐步去掉 Hook”即可完成。New Core 当前借用了 `hoi3_tfh.exe` 提供的权威世界状态、数据解析、日结算、战争、AI、补给、存档、渲染和输入循环；独立重实现必须自己拥有这些系统。更准确的工程判断是：

- New Core 已接近完成“原版扩展运行时与逆向验证平台”的第一阶段；
- New Core 尚未进入“独立 HOI3 模拟器”的第一个可运行里程碑；
- 现有成果不是独立引擎主体，但会显著降低独立引擎的逆向风险和验收成本；
- 不应把当前注入工程直接改造成独立引擎，而应保留注入模式作为长期 Oracle，在其旁边建立新的独立可执行目标和共享语义库。

如果必须给出粗略比例，此处只能作为工程量级判断而非工期承诺：New Core 对“逆向、观测、安全接口和 2D GUI 基础”的准备度约为三分之一；对“可独立启动并运行完整战局”的产品目标约为 **5%～10%**；对“达到 Project Alice 式高兼容、可长期游玩的完整重实现”则仍低于 **5%**。原因不是现有代码质量不足，而是独立引擎的大部分工作量集中在尚未拥有的权威数据模型、脚本语义和每日模拟上。

## 6.2 Project Alice 与 New Core 的工程本质差异

Project Alice 的 `src/gamestate/system_state.hpp` 以 `sys::state::world` 持有自己的权威世界，`system_state.cpp` 自行推进日期并调度经济、军事、政治、人口、研究、AI 和事件。它不是对 Victoria 2 的外部接口层，而是完整拥有进程、状态、规则和渲染循环的游戏引擎。

Alice 还通过 `dcon_generated.txt` 和构建期生成器建立强类型数据容器，通过 ParserGenerator 生成内容解析器，通过 scenario/save 序列化区分静态定义与动态战局，通过命令队列、网络校验和 OOS 报告维持多人确定性。其 `src` 下主要一方功能目录粗略约 39 万行代码；当前 `new_core/src` 约 6.8 万行。代码行数不能直接换算完成度，但足以说明二者目前不是同一产品阶段。

| 维度 | New Core 当前形态 | Project Alice 形态 | 独立 HOI3 引擎需要的变化 |
|---|---|---|---|
| 进程所有权 | DLL 注入 `hoi3_tfh.exe`，由原版创建窗口、线程和设备 | 自己启动 EXE、窗口、更新线程和渲染循环 | 新建独立 EXE、平台层、主循环和资源生命周期 |
| 权威状态 | 通过 RVA、字段、Resolver 和 Query 读取原版对象 | `sys::state::world` 是唯一权威世界 | 建立 HOI3 World Model，不再以原版指针作为事实源 |
| 内容加载 | 主要解析 SGUI/SGFX、GUI 数据、本地化和少量地图辅助文件 | 分阶段解析地图、历史、国家、单位、科技、事件、决议等全部内容 | 建立 Clausewitz VFS、依赖图、预解析、主解析和 fixup 流程 |
| 脚本语义 | 调用原版 Lua，并提供 Native Effect/Query 桥 | 自己解析和执行 trigger/effect/event/decision | 实现 HOI3 作用域、触发器、效果、变量、flag、事件和决议 VM |
| 时间与模拟 | 原版负责 Tick 和日结算，New Core 只观察或插入动作 | 自己推进日期并运行完整 daily/monthly 调度 | 建立确定性 Scheduler、Calendar、RNG 和系统更新顺序 |
| 军事与补给 | 仅有 Unit/Leader 只读层和将领捕获专用切片 | 自己处理单位、移动、战斗、围城、损耗和 AI | 实现陆海空单位、指挥链、任务、战斗、占领、补给与天气 |
| 经济与国家 | 59 个原生效果和 18 个查询最终仍调用原版 | 经济、人口、生产、预算、研究和国家状态由自身计算 | 把 Setter/Query 转为内部 Command/View，并实现派生值重算 |
| AI | 依赖原版 Lua 与原生 AI | 拥有经济、外交、战争、战役和单位 AI | 建立战略、生产、外交、战区、路径和战术执行层 |
| 渲染与 GUI | D3D9 Overlay 加声明式 2D GUI | 自己加载地图、纹理、字体、GUI 并完成整帧渲染 | 建立独立地图渲染器、相机、地图模式、FOW、3D/2D 合成与音频 |
| 输入与命令 | Hook 原版 WndProc；GUI 动作回到原版 Lua/模拟线程 | UI 产生经过 `can_*` 校验的游戏命令 | 建立统一 Command Bus、权限校验、回放和 UI/模拟线程边界 |
| 存档 | 只管理扩展状态，并用 SaveLoaded Barrier 保护原版对象 | 自有 scenario/save 格式、版本号和恢复流程 | 建立场景缓存、完整存档、版本迁移和派生数据重建 |
| 多人确定性 | 尚未验收，直接写入按单机能力发布 | 网络命令、校验和、OOS 报告及确定性测试 | 先保证单机确定性，再设计锁步命令、校验和与 OOS 工具 |
| 兼容目标 | 二进制版本兼容：绑定 D328 地址与 ABI | 内容兼容：读取原游戏及 Mod 数据 | 从“可执行文件兼容”转向“文件格式、脚本语义和行为兼容” |

## 6.3 New Core 可以直接继承或演化的部分

以下成果不是一次性代码，应成为独立引擎的共享基础或设计原型：

1. **模块注册与生命周期**：`core_module_registry`、统一 Tick、逆序关闭和生命周期事件可以演化为独立引擎的 Service/Subsystem Registry。
2. **Capability Registry**：可从“某个原版符号是否可用”扩展为“某个内容格式、模拟子系统、渲染后端或兼容特性是否可用”。
3. **稳定 ID、Query 与 Effect 抽象**：Resolver 的裸指针部分会消失，但稳定 ID、只读 Query、受控 Effect、参数校验和事务边界应保留；在独立引擎中 Effect 应转化为内部 Command。
4. **ReverseProbeFramework**：应升级为 CompatibilityProbeFramework，同时支持原版进程报告、独立引擎报告和两者差分。
5. **SaveLoaded Barrier 的代际思想**：原生内存租约会消失，但 `worldGeneration`、快照事务、读档失效和异步资源屏障仍然必要。
6. **声明式 2D GUI**：SGUI/SGFX 解释器、数据绑定、事件、动画、列表、Tooltip、Custom Widget 和多窗口管理可以迁移到独立客户端；D3D9 Hook 宿主需要替换为独立渲染后端。
7. **启动器、诊断和测试习惯**：双模式启动器可扩展出第三种“独立引擎模式”；当前 Probe/CTest/JSONL 体系可成为差分测试和兼容报告基础。

以下部分只能保留语义，不能直接成为独立引擎主体：

- `engine_profile_hoi3_tfh_402`、RVA、ABI、IAT/VTable Hook 和 D3D9 注入链只服务于原版 Oracle；
- `hoi3_gameplay_effects` 当前调用原版 Command/Effect/Setter，独立引擎必须重新实现这些操作背后的完整规则；
- `hoi3_native_queries_module` 当前读取原版对象，独立引擎应改为查询自己的 World Model；
- `leader_capture` 当前依赖原版战斗移除路径，独立引擎中应成为军事规则系统的一部分，而非补丁模块。

## 6.4 相比 Project Alice 尚缺少的核心工程

### 6.4.1 内容兼容与场景编译

- 支持原版目录、TFH 目录和多个 Mod 根的覆盖顺序、`replace_path` 与文件来源追踪；
- 解析 HOI3 的 localization、defines、map、history、common、units、technologies、events、decisions、interface、gfx、music 等目录；
- 建立名称预注册、依赖解析、作用域绑定、后处理和错误恢复，避免文件加载顺序成为隐式全局状态；
- 生成版本化 Scenario Cache，使新战役不必每次重新解析全部文本和地图资源；
- 对未知字段、重复定义、缺失引用和 Mod 覆盖冲突提供可定位诊断。

### 6.4.2 权威 World Model

- 为国家、省份、地区、单位、旅、将领、科技、研究、生产、外交关系、战争、战斗、补给节点、事件实例等定义稳定 ID；
- 建立所有权、控制权、核心、阵营、同盟、指挥链、单位隶属、战斗参与和补给图等关系；
- 区分静态 Scenario Definition、动态 Save State、派生 Cache 和仅客户端 UI 状态；
- 支持创建、销毁、压缩存储、遍历、跨线程只读快照和读档后的句柄失效；
- 建立类似 Alice DataContainer 的 Schema/代码生成机制，避免手写数百种对象访问器和序列化代码。

### 6.4.3 Clausewitz 脚本运行时

- 完整解析并执行 trigger、effect、modifier、event、decision、on_action 和 AI 脚本；
- 准确实现 `ROOT/FROM/THIS` 等作用域、迭代器、随机列表、变量、flag、日期、持续时间和延迟事件；
- 区分只读条件与写效果，提供统一诊断、调用栈、执行预算和确定性 RNG；
- 建立脚本兼容测试集，验证原版和主要 Mod 的解析结果及行为差异。

### 6.4.4 确定性模拟内核

- 自己推进游戏日期、暂停、速度和日/月/年调度；
- 明确每个系统的更新顺序、读写阶段、并行边界和派生数据重算；
- 实现可复现 RNG、命令队列、事件队列、延迟任务和回放日志；
- 为同一场景、同一命令流建立跨运行 checksum，先解决单机确定性再考虑多人。

### 6.4.5 HOI3 玩法系统

- **军事**：OOB/指挥链、陆海空单位、编制、移动、路径、运输、任务、组织度、兵力、战斗宽度、增援、撤退、围歼、占领与投降；
- **补给**：补给源、基础设施、港口、运输船、库存、吞吐、路径、拥堵、油料、海外补给与重算；
- **经济与生产**：IC、资源、消费品、补给品、增援、升级、生产队列、建筑、贸易和运输损失；
- **科技与人力**：领导力分配、研究、理论/实践、军官池、人力、动员和单位属性重算；
- **政治外交情报**：政党、意识形态、法律、部长、凝聚力、威胁、中立、关系、阵营、战争、条约、间谍和情报；
- **AI**：国家战略、外交、生产、研究、战区、前线、兵力评估、路径、任务分配、进攻、防御、撤退及性能预算。

### 6.4.6 独立客户端与工具链

- 独立窗口、渲染设备、地图网格、地形、边界、河流、海岸、单位模型、FOW、天气、地图模式和相机；
- 完整 GUI、输入焦点、快捷键、拖放、可访问性、分辨率适配、字体、本地化、音频和音乐；
- Scenario Compiler、内容检查器、存档查看器、脚本调试器、地图调试器、性能分析器和崩溃报告；
- Windows 以外的平台抽象；若以兼容重实现为目标，应避免把新的核心继续绑定到 Win32/D3D9。

### 6.4.7 存档、多人和兼容性验证

- 自有版本化 Scenario/Save 格式、压缩、迁移、损坏检测和派生数据恢复；
- 原版文本存档读取可作为后续兼容目标，但不应阻塞第一版自有存档；
- 多人需要权威命令流、主机权限、掉线/重连、校验和、OOS 报告和确定性回放；
- 建立原版基准战局、每日快照、事件结果、战斗结果和 AI 决策的 Golden Trace；
- 对原版与独立引擎做差分测试，而不是仅检查“没有崩溃”。

## 6.5 可验收的最小独立系统

独立重实现的第一个有效验收目标不应是“显示一张地图”，而应是一个可重复、可保存、可对照原版的 **Headless Scenario Kernel**。最小系统应满足：

1. `hoi3_reimpl.exe` 不加载或启动 `hoi3_tfh.exe`；
2. 从用户指定的 HOI3/TFH 安装目录和 Mod 描述符建立虚拟文件系统；
3. 解析地图、省份、国家、历史、单位定义、科技、事件与决议的最小闭包；
4. 构建带稳定 ID 的权威 World Model，并能载入一个 1936 场景；
5. 以固定 RNG 和命令流连续推进至少 365 天，两次运行产生相同 checksum；
6. 执行一组真实 trigger/effect/event/decision，并输出可与原版 Query 快照比较的结果；
7. 保存后退出，再读取存档继续推进，checksum 与连续运行一致；
8. 最后再接入独立地图窗口，显示省份、国家、日期和最小单位移动。

达到这一目标，才表示工程真正从“原版进程扩展”跨入“独立兼容引擎”。在此之前，即使拥有独立窗口和地图渲染，也只能算内容查看器或客户端原型。

## 6.6 推荐的双轨工程架构

建议长期保留两条相互验证的产品线：

### A. `new_core`：原版 Oracle 与扩展运行时

- 继续负责版本化符号、稳定对象、Query/Effect、ReverseProbe 和实机行为采样；
- 为独立引擎提供原版状态快照、事件执行结果、战斗/补给轨迹和每日 Golden Trace；
- 不再承担独立世界模型，也不为了复用而把 Hook 语义渗入新引擎。

### B. 新建独立引擎目标

建议未来按责任拆分，而不是继续把全部代码放入 `new_core/src`：

- `hoi3_content`：VFS、Mod 覆盖、文本解析、依赖图和 Scenario Compiler；
- `hoi3_world`：Schema、稳定 ID、关系、存储、序列化和快照；
- `hoi3_script`：Trigger/Effect/Event/Decision VM；
- `hoi3_sim`：日循环、命令、经济、政治、外交、军事、补给和 AI；
- `hoi3_client`：窗口、地图、渲染、GUI、输入、音频和本地化；
- `hoi3_compat`：Oracle 报告导入、Golden Trace、差分比较和兼容等级；
- `hoi3_tools`：场景编译器、验证器、存档工具和调试器。

可共享的是稳定数据语义、GUI 文档模型、诊断格式、Capability/Probe 概念和测试协议；不可共享的是原版地址、ABI、Hook、原生对象指针和 D3D9 注入宿主。

## 6.7 推荐推进顺序

1. **冻结独立引擎边界**：定义独立 EXE、共享库与 `new_core` Oracle 的依赖方向，禁止新引擎依赖任何 RVA 或原版对象地址。
2. **建立 VFS 与内容清单器**：先能解释安装目录、TFH、Mod、`replace_path` 和文件来源，再写具体系统解析器。
3. **建立 World Schema 生成器**：先定义国家、省份、关系和静态定义/动态状态分层，再扩展对象种类。
4. **实现 Scenario Compiler**：按依赖阶段加载地图、国家、历史、单位、科技、事件和决议，输出版本化缓存及完整诊断。
5. **实现 Headless 日循环**：日期、RNG、命令队列、事件队列、checksum、保存/读取先形成闭环。
6. **实现脚本最小闭包**：优先支持当前 Mod 和基准场景实际使用的 trigger/effect，而不是一次性追求全部语法。
7. **完成军事/补给纵向切片**：选择一个受控战区，实现单位移动、补给、战斗、占领与 AI 的最小完整闭环。
8. **接入独立地图和 GUI**：复用 SGUI 的声明式层，但替换 D3D9 Hook 宿主，建立独立渲染和输入系统。
9. **扩大经济、政治、外交和 AI**：每个子系统都用原版 Golden Trace 验收，而不是依赖主观体验。
10. **最后推进多人和广泛 Mod 兼容**：单机确定性、存档稳定和基准战局未完成前，不提前承诺多人兼容。

## 6.8 对 Project Alice 源码的使用边界

Project Alice 非常适合作为架构、加载顺序、数据容器、确定性、工具链和测试组织方式的参考，但不能把“参考设计”误解为“直接复制实现”。仓库中的 Project Alice 使用 GPLv3；如果未来直接复制、修改或链接其受保护代码，需要先明确 New Core/独立引擎的许可证并履行相应 GPL 义务。若暂时没有决定采用 GPLv3，应只研究公开架构和行为，自行实现 HOI3 专用的数据模型、解析器与模拟规则，并保留来源和设计决策记录。

同样，独立兼容引擎应默认要求用户提供合法的 HOI3/TFH 安装数据；开源仓库只发布引擎、兼容层和自有资源，不直接重新分发原版专有地图、图片、字体、音乐或文本内容。

## 6.9 本阶段结论

New Core 已经让“独立 HOI3 重实现”从纯粹设想变成了可以采用差分工程方法推进的长期项目，但当前最有价值的成果是 **观测原版、证明对象、记录行为和隔离风险**，而不是已经重写了 HOI3 的主体。Project Alice 展示的真正门槛不是 Hook 数量或 Setter 数量，而是：拥有自己的权威世界、完整内容编译链、脚本运行时、确定性日循环、玩法模拟、存档和兼容测试。

因此，未来正确方向不是废弃 New Core，也不是继续无限堆叠原生 Setter，而是让 New Core 成为原版 Oracle，同时以独立仓库子工程或独立顶层目标启动 `hoi3_content + hoi3_world + hoi3_script + hoi3_sim`。当 Headless Scenario Kernel 能稳定推进一年、保存读取一致并与原版关键快照可比较时，才算真正迈出兼容重实现的第一步。
