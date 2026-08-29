# HOI3 完整逆向工程可行性与最小验收系统备忘录

> 建立日期：2026-08-22  
> 当前目标版本：`hoi3_tfh.exe`，PE32/x86，时间戳 `0x50978B2F`，映像大小 `0x018B0000`  
> 当前工程：`new_core` 进程内扩展核心  
> 关联文档：`HOI3原生数据接口分类与逆向开发备忘录.md`、`Project-Dillen/hoi3oracle/docs/CORE_ARCHITECTURE.md`

## 1. 结论

### 1.1 有没有希望逆向整个 HOI3

**有希望，但必须先明确“整个 HOI3”指什么。**

以下三个目标的可行性完全不同：

1. **完整扩展接口逆向**：掌握主要游戏对象、数据、命令、效果、生命周期和各子系统入口，让 `new_core` 可以安全读取、修改并扩展游戏。这个目标可行，也是当前工程最合理的长期目标。
2. **行为级完整逆向**：记录所有主要子系统的规则、状态转移和关键算法，使我们能够解释并重现 HOI3 的主要行为。这个目标可行，但工作量很大，需要长期积累测试和行为对照。
3. **恢复原始源码或逐指令等价重制**：无法真正恢复原开发者的变量名、注释、工程结构和全部设计意图。编译优化已经永久丢失这些信息。可以做兼容重实现，但不能把它称为“恢复原源码”。

因此，推荐把最终目标定义为：

> **针对受支持的 HOI3 版本，建立一套经过版本验证、生命周期安全、可查询、可执行、可诊断、可存档验证的原生 Engine API，并逐步覆盖全部主要游戏子系统。**

这个目标比“给每个功能单独找一个地址”更严格，也比“重新写一遍 HOI3”更现实。

### 1.2 当前工程是否已经具备起点

是。`new_core` 已经具备逆向平台的骨架：

- 注入启动器与版本化握手；
- 模块注册表；
- 统一 Hook 注册、安装、维护和卸载；
- 统一 Frontend/Gameplay/Save 生命周期；
- Lua 5.1 Hook 与原生绑定；
- D3D9 Hook、Script GUI 和输入系统；
- 通用 `NativeEffectService`；
- Script GUI 与将领捕获两个实际模块；
- 离线 Probe 和 CTest 验证体系。

目前缺少的不是“能否注入”，而是面向逆向工程的统一符号、类型、对象、查询、能力和测试基础设施。

## 2. 必须区分的两个最终产品

### 2.1 产品 A：HOI3 原生扩展核心

这是当前 `new_core` 的自然发展方向。

目标：

- 游戏仍由原版 `hoi3_tfh.exe` 运行；
- `new_core` 注入进程；
- 原版负责模拟、AI、存档、渲染和资源加载；
- `new_core` 暴露稳定查询与效果接口；
- Lua、Script GUI 和其他模块通过接口扩展游戏；
- 所有版本差异由 Engine Profile 隔离。

这是最值得优先完成、也最可能达到高覆盖率的“完整逆向”。

### 2.2 产品 B：HOI3 兼容重实现

目标是脱离原版进程，自己实现：

- 数据文件解析；
- 世界状态；
- 每日模拟；
- 经济、生产、科技、外交、战斗、补给和 AI；
- GUI、地图、音频、输入；
- 存档兼容；
- Mod 兼容；
- 可能还包括多人同步。

这属于多年期独立引擎工程。它可以复用产品 A 得到的行为文档、类型图和测试用例，但不应成为当前阶段的直接目标。

## 3. “完整逆向”不能用函数数量验收

二进制中有大量：

- 编译器生成代码；
- 模板实例；
- 重复容器函数；
- 错误处理路径；
- UI 辅助函数；
- 不再使用的历史代码；
- 内联后无法恢复边界的逻辑。

所以“命名了 80% 函数”并不等于完成，“只命名了 20% 但所有状态变化都有稳定接口”反而可能更有价值。

建议采用以下验收定义：

1. **对象覆盖**：核心对象有稳定标识、生命周期和类型说明。
2. **状态覆盖**：主要持久化状态均可读取，必要状态可通过原生路径修改。
3. **行为覆盖**：关键规则有可重复的输入/输出测试。
4. **子系统覆盖**：每个主要子系统至少有一个完整垂直切片。
5. **存档覆盖**：接口修改的数据能够正确保存、读取和回滚。
6. **版本覆盖**：所有地址和结构偏移归属于明确版本配置。
7. **故障覆盖**：错误版本、失效对象和错误线程不会导致游戏崩溃。

## 4. 最小可验收逆向系统（MRS）

这里的 MRS 是 **Minimum Reverse-engineering System**。它不是三个临时 Hook，而是一套能够证明后续可规模化逆向的最小平台。

### 4.1 MRS 的目标

MRS 必须证明我们能够统一处理五种典型问题：

1. **标量状态**：例如人力、国家凝聚力。
2. **容器状态**：例如资源库存。
3. **有期限效果**：例如省份 modifier。
4. **对象关系**：例如省份控制者、单位与将领关系。
5. **派生状态**：例如军官度、总 IC，需要修改源数据并触发重算。

如果系统只会直接写一个浮点数，就不能证明它能够支撑完整逆向。

### 4.2 MRS 必须新增或补齐的核心设施

### A. Engine Version Profile

把目前散落在 `hoi3_lifecycle.cpp`、`gui_d3d9_hook.cpp` 和 `leader_capture_engine.cpp` 中的版本常量集中管理。

每个 Profile 至少包含：

```text
build_id
exe_name
machine
timestamp
image_size
optional_sha256
symbols
globals
structures
signatures
capabilities
```

示意：

```cpp
struct EngineSymbolProfile
{
	std::string name;
	uint32_t rva = 0;
	std::vector<uint8_t> signature;
	std::string callingConvention;
};

struct EngineBuildProfile
{
	std::string id;
	uint32_t timestamp = 0;
	uint32_t imageSize = 0;
	std::unordered_map<std::string, EngineSymbolProfile> symbols;
	std::unordered_map<std::string, uint32_t> fieldOffsets;
};
```

验收要求：错误版本必须拒绝安装写入型 Hook，不允许“地址差不多就继续运行”。

### B. Engine Symbol Registry

模块不再自己保存 RVA。模块只能按名称请求符号：

```cpp
auto function = symbols.Resolve<ChangeManpowerFn>(
	"country.change_manpower"
);
```

Registry 负责：

- Profile 查找；
- RVA 转 VA；
- 范围验证；
- 指令签名验证；
- 调用约定说明；
- 可用能力报告。

### C. Engine Type Registry

记录已经验证的对象结构，而不是让每个模块分别写 `object + 0x123`。

至少需要：

- `GameState`；
- `Country`；
- `Province`；
- `Unit`；
- `Leader`；
- `ModifierInstance`；
- `GoodsPool`；
- 保存与对象 ID 相关的结构。

结构描述必须区分：

- 已确认字段；
- 推测字段；
- 仅某版本有效字段；
- 指针、句柄、索引和内嵌对象。

### D. Stable Object Handle

公共 API 不能暴露裸指针。建议统一使用：

```text
CountryHandle  = Tag + lifecycle generation
ProvinceHandle = Province ID + lifecycle generation
UnitHandle     = persistent unit ID + lifecycle generation
LeaderHandle   = leader ID + lifecycle generation
```

每次执行前由 Object Resolver 重新解析对象。读档、退出战局和玩家切换后，旧句柄必须失效。

### E. Native Query Service

当前已有 `NativeEffectService`，但完整逆向还需要与它对称的只读查询层：

```lua
local ok, value, code = NewCoreNative.Query(
	"country.manpower",
	{ tag = "CHI" }
)
```

Query 必须：

- 只读；
- 有类型化返回值；
- 能批量查询；
- 能报告字段来源和能力状态；
- 不要求 GUI 存在；
- 不把裸地址返回 Lua。

### F. Native Effect Service

继续使用现有事务模型，并补充真实 HOI3 Handler：

- 参数准备；
- 游戏线程执行；
- 生命周期检查；
- 修改前快照；
- rollback；
- 派生数据重算；
- 存档验证。

### G. Game-thread Dispatcher

所有会修改游戏的调用必须在确认过的模拟线程执行。

当前 Native Effect 会绑定首次成功调用线程；MRS 还应明确：

- 当前线程 ID；
- 可执行阶段；
- Tick 边界；
- 是否允许同步调用；
- 是否需要排入下一安全点；
- 超时和取消语义。

### H. Capability Registry

Lua 和模块必须能够查询当前版本支持什么：

```lua
NewCoreNative.HasQuery("country.manpower")
NewCoreNative.HasEffect("province.add_modifier")
NewCoreNative.GetCapabilities()
```

缺失能力应产生明确错误，而不是静默失败或访问空地址。

### I. Reverse Probe Framework

Probe 不应只测试自己的算法，还要验证游戏进程中的真实符号与行为：

- Profile 匹配；
- 符号签名；
- 对象解析；
- 只读值与 Lua 值一致；
- 效果前后值；
- 次日保持；
- 存档往返；
- Frontend 拒绝；
- 错误对象拒绝；
- 重复调用稳定性。

### J. Trace 与诊断

至少记录：

```text
build_id
lifecycle_generation
save_generation
thread_id
operation/query
stable_object_id
symbol_name
result_code
duration
before/after summary
```

日志禁止长期保存裸对象地址；地址只可用于当次调试诊断。

### 4.3 MRS 的最小功能接口

### 第一组：只读查询

| 接口 | 用于验证的模式 |
| --- | --- |
| `runtime.build` | 版本 Profile |
| `runtime.lifecycle` | 生命周期 |
| `country.manpower` | 国家标量 |
| `country.goods` | 容器元素 |
| `country.national_unity` | 政治标量 |
| `country.total_ic` | 派生读取 |
| `province.owner` | 对象关系 |
| `province.controller` | 对象关系 |
| `province.building_level` | 结构化字段 |
| `province.modifiers` | 动态容器 |
| `unit.leader` | 跨对象关系 |
| `leader.country` | 将领池关系 |

### 第二组：原生效果

| 接口 | 用于验证的模式 |
| --- | --- |
| `country.add_manpower` | 标量写入 |
| `country.add_goods` | 容器写入 |
| `country.add_national_unity` | 有边界的标量写入 |
| `province.add_modifier` | 有期限对象 |
| `province.remove_modifier` | 对象删除 |
| `province.set_controller` | 复杂关系与重算 |
| `leader.transfer_pool` | 对象图修改 |

其中 `province.set_controller` 和 `leader.transfer_pool` 不必最先完成，但 MRS 最终验收需要至少一个复杂对象关系写入。现有将领捕获模块可作为这部分的逆向基础。

### 4.4 MRS 的验收用例

### 启动与版本

- 正确版本能够注入并完成握手；
- 错误版本只允许安全只读诊断，不安装写入 Hook；
- 所有已注册符号通过地址范围与签名验证；
- Capability 列表与实际 Handler 完全一致。

### 生命周期

- 主菜单中所有游戏数据 Effect 返回 `gameplay_inactive`；
- 进入战局后 Query 正常；
- 退出战局后所有对象句柄失效；
- 读档后 generation 改变，旧对象不得继续使用；
- 玩家 Tag 切换后国家上下文正确刷新。

### 查询一致性

- Native Query 的人力、资源、凝聚力、IC、owner/controller 与 Lua 标准接口一致；
- 同一帧重复读取结果稳定；
- 批量查询不会改变游戏状态；
- 无效 Tag、Province ID、Unit ID 返回可诊断错误。

### 写入一致性

- 人力和资源修改在调用后立即可读；
- 数值不会在下一日 Tick 被旧缓存覆盖；
- 国家凝聚力被限制在合法范围；
- Modifier 正确添加、刷新、到期和删除；
- 复杂对象关系修改会触发所有必要重算与通知。

### 事务

以下批次必须全部成功或全部回滚：

```text
扣除国家人力
添加省份限时 modifier
写入任务完成 variable
```

任意一步失败时，其他步骤不得保留半完成状态。

### 存档

- 修改后保存，完全退出进程，再次启动并读档；
- 人力、库存、modifier、对象关系与保存时一致；
- 读取较早存档后恢复为较早状态，而不是保留进程内最后状态；
- 主菜单不残留上一个战局的对象和任务状态。

### 稳定性

- 连续进入/退出战局至少 20 次；
- 连续读取不同存档至少 20 次；
- 高速运行至少 30 个游戏日；
- Query 压力测试不产生明显 Tick 卡顿；
- 空闲注入模式不改变原版模拟结果；
- 无句柄泄漏、Hook 重复安装或持续内存增长。

### 4.5 MRS 的验收产物

MRS 完成时，仓库至少应存在：

```text
new_core/engine_profiles/
new_core/src/engine_symbol_registry.*
new_core/src/engine_type_registry.*
new_core/src/engine_object_resolver.*
new_core/src/native_query_bridge.*
new_core/src/native_effect_bridge.*
new_core/src/reverse_probe_runtime.*
new_core/tests/reverse_contracts/
docs/engine_api/
```

具体目录可以调整，但职责不能重新散回各业务模块。

## 5. 当前工程距离 MRS 还有多远

| 能力 | 当前状态 | MRS 缺口 |
| --- | --- | --- |
| 注入与启动器 | 已完成基础闭环 | 增加 Profile/Capability 结果展示 |
| 模块注册 | 已完成 | 无关键缺口 |
| Hook 注册 | 已完成 | Hook 仍需统一使用符号注册表 |
| 生命周期 | 已完成基础阶段 | 需要可靠原生 SaveLoaded Hook |
| Lua 绑定 | 已完成基础链路 | 增加通用 Query 与 Capability API |
| Native Effect | 通用事务桥已完成 | 尚无正式 HOI3 数据 Handler |
| 版本配置 | 只有分散常量 | 必须集中为 Engine Profile |
| 类型系统 | 未建立 | 必须建立字段与可信度注册表 |
| 对象句柄 | 各模块自行处理 | 必须统一稳定 ID 与 generation |
| Query | GUI 数据桥可读部分数据 | 缺少与 GUI 无关的原生 Query Service |
| Probe | 离线 Probe 很丰富 | 缺少真实进程合同测试框架 |
| 存档 | GUI 持久化已验证一部分 | 原生数据 Handler 尚未做跨进程往返 |

结论：基础设施完成度已经较高，但“逆向工程 SDK”仍处于起步阶段。最优先的不是继续增加零散机制，而是补齐 Profile、Symbol、Type、Object 和 Query 五个公共层。

## 6. 完整扩展接口逆向需要覆盖的系统

### 6.1 启动、平台与进程

- 可执行文件版本识别；
- 模块加载顺序；
- 内存分配器；
- 主线程、模拟线程、渲染线程；
- 异常处理；
- 文件系统与 Mod 搜索路径；
- 命令行参数；
- 主循环和退出流程。

### 6.2 对象模型

- 对象基类、RTTI、虚表；
- 对象 ID 与注册表；
- Country、Province、Unit、Leader、War、Faction 等布局；
- 容器类型、智能引用或手工引用关系；
- 创建、销毁、转移、序列化；
- 跨 Tick 和跨读档生命周期。

### 6.3 数据库与脚本

- Lua 类绑定与构造器；
- `common`、`history`、`events`、`decisions`、`units`、`map` 等文本解析；
- 数据库对象建立顺序；
- Effect、Trigger、Scope；
- 国家变量、Flag 与全局状态；
- 本地化键和值；
- Mod 覆盖和合并规则。

### 6.4 国家经济

- 人力；
- IC 来源与有效 IC；
- 生产滑条；
- 金钱与七类物资；
- 贸易；
- 运输船和护航舰；
- 消费品、不满度；
- 领导力、军官、外交点、间谍；
- 国家与省份 modifier；
- 每日收入、支出、需求和平衡计算。

### 6.5 生产与建设

- 单位和建筑成本；
- 建设时间；
- 序列与并行；
- 预备役；
- 实用经验与生产加成；
- 增援、升级、补给生产；
- 队列排序、取消、完成；
- 完成后对象创建。

### 6.6 省份、地图与补给

- Province 数据；
- owner、controller、core；
- 建筑、资源、人力、领导力、胜利点；
- 地形、天气、基础设施；
- 邻接、海峡、港口、机场；
- 补给节点、路径、吞吐、税损和库存；
- 战略转移；
- 迷雾、情报和地图模式。

### 6.7 军事单位与指挥链

- 旅、师、空军、海军；
- 兵力、组织度、经验、补给、燃料；
- 单位属性聚合；
- 指挥链和 HQ；
- 将领池、任命、经验、特性、死亡与被俘；
- 单位创建、解散、歼灭、合并、拆分和转移；
- 远征军与盟军控制。

### 6.8 战斗系统

- 战斗创建与结束；
- 攻防双方；
- 宽度、预备队、增援；
- 命中、伤害、组织度损失；
- 地形、天气、将领、科技与补给修正；
- 撤退、追击、包围与歼灭；
- 陆战、海战、空战的差异；
- 战斗事件与统计。

### 6.9 外交、战争与阵营

- 国家关系；
- 威胁、中立度、外交距离；
- 联盟、阵营、傀儡、保证独立；
- 宣战、和平、停战；
- 战争对象和胜利条件；
- 军事通行、禁运、互不侵犯；
- 贸易、债务、生产许可；
- 外交 Action 的验证、接受和执行。

### 6.10 政治、情报与科技

- 政府、意识形态、党派支持与组织度；
- 法律和部长；
- 国家凝聚力、不满度和投降度；
- 间谍生产、部署、任务和效果；
- 科技等级、研究进度、难度和年份；
- 科技完成后的属性重算。

### 6.11 AI

- 国家战略；
- 外交、生产、科技、政治、情报部长 AI；
- 战区创建和目标；
- 兵力需求评估；
- 战术进攻、防守、撤退和增援；
- 海空任务；
- 路径、补给和运输评估；
- 难度和随机性来源。

Lua 只能覆盖 AI 的部分战略和部长决策。要解释战术进攻倾向，必须进入原生 AI 评分和命令生成链。

### 6.12 GUI、渲染、输入与音频

- D3D9 设备生命周期；
- 原版 GUI 对象树；
- Sprite、字体、动画和 Effect；
- 地图渲染；
- 3D 单位模型；
- 鼠标、键盘和窗口消息；
- 音乐、音效和混音；
- 分辨率、全屏和窗口模式。

当前 Script GUI 已覆盖新 2D GUI，但这不等于原版 GUI 和 3D 渲染已经逆向完成。

### 6.13 存档与确定性

- 每类对象的序列化；
- 对象 ID 重建；
- SaveLoaded 的真实边界；
- 缓存重算；
- RNG 状态；
- 每日 Tick 顺序；
- 多人命令同步；
- OOS 检测与恢复。

如果目标只支持单机，可以降低多人网络优先级；如果宣称“完整 HOI3”，则不能永久忽略确定性和多人系统。

## 7. 完整逆向的分级验收

### R0：可识别与可注入

- 识别目标版本；
- 安全注入；
- 握手、日志、退出；
- 错误版本拒绝。

当前基本达到。

### R1：生命周期与稳定对象

- Frontend/Gameplay/Save 生命周期；
- Country、Province、Unit、Leader 稳定句柄；
- 跨读档失效策略。

当前部分达到。

### R2：通用读写 API

- Query、Effect、Capability；
- 国家经济和省份 modifier 垂直切片；
- 原子事务；
- 跨进程存档验证。

这是下一阶段，也是 MRS 的主体。

### R3：主要玩法子系统

- 经济、生产、科技、政治、外交、情报；
- 省份、单位、将领；
- 统一对象与状态 API。

达到后可以认为“绝大多数 Mod 机制无需事件中转”。

### R4：战斗、补给与战术 AI

- 战斗全过程；
- 补给网络；
- 战区和战术命令；
- 可解释的 AI 评分与 Hook。

这是难度显著上升的阶段。

### R5：原版 GUI、地图与 3D

- 原版 UI 对象树；
- 3D 地图对象；
- 模型、动画、Effect；
- 输入和音频。

达到后可构建深度整合的新界面和 3D 功能。

### R6：存档、确定性与多人

- 完整序列化图；
- 确定性 Tick；
- 命令同步；
- 多人兼容。

### R7：行为兼容重实现资料

- 每个子系统有行为规范；
- 可重复输入/输出测试；
- 可以由独立实现按规范复现；
- 不依赖复制原版源码。

R7 是构建兼容新引擎的资料基础，不代表自动获得原始源码。

## 8. 应该怎么逆向

### 8.1 固定目标版本

首先只支持当前 TFH 构建：

```text
timestamp  = 0x50978B2F
image_size = 0x018B0000
machine    = i386
```

在第一套 Profile 稳定前，不同时支持多个版本，否则每个结论都会被版本差异污染。

### 8.2 从 Lua 接口建立类型锚点

`LUAClassReference.wiki` 是很好的类型目录。方法：

1. 找到 Lua 类注册表；
2. 定位构造器和方法绑定函数；
3. 从绑定函数回溯真实 C++ 类型和 this 指针；
4. 将 Lua getter 与对象字段对应；
5. 找到同字段的内部 writer 或 effect；
6. 把 reader/writer 注册进统一 API。

Lua 暴露的 124 个类可以作为第一批对象和方法命名锚点。

### 8.3 从事件 Effect 追踪写入链

对于人力、国家 buff、省份 buff、国家凝聚力等数据：

1. 制作只改变一个数值的测试事件；
2. 在数值字段或候选效果分派函数设置断点；
3. 触发事件；
4. 记录调用栈、参数和修改前后状态；
5. 找出最小原生写入函数；
6. 确认它是否调用重算、通知和序列化标记；
7. 用 `NativeEffectService` 包装，不再通过事件调用。

事件只用于定位内核路径，不作为最终运行机制。

### 8.4 差分实验

每次只改变一个变量：

- 两份存档做二进制/文本差分；
- 两次内存快照做差分；
- 对字段设置写入断点；
- 比较下一日 Tick 前后；
- 比较保存、退出、重启、读档后。

必须避免一次测试同时改变多个法律、资源和单位，否则无法确定因果关系。

### 8.5 静态分析

建议建立统一命名数据库：

- 导入、字符串、RTTI、虚表；
- Lua 注册名；
- 事件 Effect 名；
- 文件路径和日志字符串；
- 函数调用图；
- 交叉引用；
- 已知 RVA 与签名。

每个已命名函数应记录：

```text
symbol name
RVA
calling convention
arguments
return type
object type
side effects
thread requirement
lifecycle requirement
save impact
confidence
evidence
```

### 8.6 动态分析

主要手段：

- 函数断点；
- 数据写入断点；
- 调用栈记录；
- 单步确认调用约定；
- 对象内存布局观察；
- Hook 前后参数日志；
- 单变量行为实验。

探索阶段尽量只读；确认调用约定和参数前，不直接调用未知函数。

### 8.7 保存与回放测试

为每个效果建立固定测试场景：

```text
加载基线存档
记录 Query 快照
执行单一 Effect
记录即时快照
运行一天
记录 Tick 后快照
保存并退出
重启并读档
记录恢复快照
比较预期
```

这是判断“找到了真正写入函数”还是“只改了临时缓存”的关键。

### 8.8 调用约定与 x86 封装

HOI3 是 x86 程序，必须明确：

- `__thiscall`；
- `__cdecl`；
- `__stdcall`；
- 寄存器传参；
- 栈清理方；
- 返回对象和隐藏参数；
- 浮点与 `CFixedPoint` 表示。

所有调用都应通过类型化 Wrapper，业务模块禁止自行编写裸函数指针转换。

## 9. 推荐工程架构

```text
new_core
├─ core
│  ├─ module_registry
│  ├─ hook_registry
│  ├─ lifecycle
│  └─ game_thread
├─ engine
│  ├─ build_profile
│  ├─ symbol_registry
│  ├─ type_registry
│  ├─ object_resolver
│  ├─ fault_boundary
│  └─ capability_registry
├─ api
│  ├─ native_query
│  ├─ native_effect
│  ├─ lua_binding
│  └─ diagnostics
├─ systems
│  ├─ country
│  ├─ province
│  ├─ unit
│  ├─ leader
│  ├─ modifier
│  ├─ economy
│  ├─ diplomacy
│  ├─ battle
│  └─ ai
├─ modules
│  ├─ script_gui
│  └─ leader_capture
└─ probes
   ├─ offline
   ├─ inprocess
   └─ save_roundtrip
```

这是职责结构示意，不要求立即移动现有文件。迁移应逐步进行，避免大规模重命名破坏当前可运行版本。

## 10. 逆向资料的可信度等级

每个字段和函数必须标注：

| 等级 | 定义 |
| --- | --- |
| `Observed` | 只观察到相关性，尚未确认因果 |
| `Candidate` | 静态与动态证据基本一致，但未完成存档验证 |
| `VerifiedRead` | 多场景读取与 Lua/界面一致 |
| `VerifiedWrite` | 写入即时、次日和读档均正确 |
| `Production` | 有版本签名、错误隔离、回滚、压力测试和文档 |

只有 `Production` 能力可以默认暴露给普通 Script GUI。其他能力只能在开发模式启用。

## 11. 风险

### 11.1 技术风险

- C++ 类型信息不完整；
- 编译器内联破坏函数边界；
- 同一字段有多个缓存；
- 写入函数依赖隐藏上下文；
- 对象在 Tick 中销毁；
- Hook 点在不同场景调用约定不同；
- 保存时存在异步或延迟队列；
- Lua 与模拟线程并非所有时候相同。

### 11.2 工程风险

- RVA 和偏移继续散落在业务模块；
- 同一概念出现多套名字；
- 没有证据记录，数月后无法判断结论来源；
- 只做成功路径，不测试 Frontend、读档和错误对象；
- 为快速展示直接写缓存，后续难以修复。

### 11.3 范围风险

“完整逆向”很容易无限扩张。必须以 R0-R7 分级和子系统验收表控制范围。当前优先级应停留在 R2，而不是同时进入战术 AI、3D、音频和多人。

### 11.4 发布边界

- 不在仓库中分发原版可执行文件、资源或大段反编译代码；
- 公开接口应使用自行命名的结构和行为说明；
- 如未来制作独立兼容引擎，应采用干净室方式分离“行为规范”和“实现”；
- 注入功能应面向单机 Mod，不用于破坏多人公平性或绕过平台保护。

## 12. 推荐推进顺序

### 阶段 0：冻结基线

- 固定目标 EXE；
- 保存 SHA-256；
- 建立基线存档；
- 建立可重复启动和注入脚本；
- 记录当前所有已知 RVA。

### 阶段 1：逆向 SDK 基础

- Engine Profile；
- Symbol Registry；
- Type Registry；
- Object Resolver；
- Capability Registry；
- Fault Boundary。

### 阶段 2：Query 闭环

- 国家、资源、省份、单位、将领查询；
- 与 Lua 标准接口交叉验证；
- 批量 Query 与性能测试。

### 阶段 3：Effect 闭环

- `country.add_manpower`；
- `country.add_goods`；
- `province.add/remove_modifier`；
- 原子事务；
- 运行一天与存档往返。

### 阶段 4：复杂对象关系

- Province controller；
- Leader pool；
- Unit/Leader 关系；
- 复用并改造现有将领捕获代码。

### 阶段 5：达到 MRS

- 完成全部 MRS 验收；
- 发布第一版 Engine API 文档；
- 普通模块不再使用裸 RVA；
- Script GUI 可通过 Query/Effect 完成战争地图任务。

### 阶段 6：扩展主要玩法

- 国家经济；
- 生产；
- 科技；
- 政治；
- 外交；
- 情报；
- 单位和省份。

### 阶段 7：高难度系统

- 补给；
- 战斗；
- 战区；
- 战术 AI；
- 原版 GUI/3D；
- 存档序列化全图；
- 多人确定性。

## 13. 当前最合理的下一步

不要立即尝试同时逆向人力、IC、资源、军官、补给、单位和 AI。下一步应先建立：

1. `EngineBuildProfile`；
2. `EngineSymbolRegistry`；
3. `EngineTypeRegistry`；
4. `EngineObjectResolver`；
5. `NativeQueryService`。

然后用三个正式写接口验证平台：

1. `country.add_manpower`；
2. `province.add_modifier`；
3. `province.remove_modifier`。

这三项完成后，再添加 `country.add_goods` 和一个复杂对象关系接口。达到 MRS 后，才能确认工程已经从“多个成功的注入功能”升级为“可以持续逆向整个 HOI3 的平台”。

## 14. 最终验收定义

### 完整扩展接口逆向完成

可以在满足以下条件时宣布：

- R0-R6 按目标范围完成；
- 所有主要持久化状态有稳定 Query；
- 所有 Mod 常用状态变化有原生 Effect 或明确不支持原因；
- 所有公开接口达到 `Production` 可信度；
- 地址、偏移和调用约定全部归入版本 Profile；
- 不依赖业务模块中的裸指针和裸 RVA；
- 进入、退出、读档和跨进程存档均稳定；
- 主要子系统有行为合同和回归测试；
- 空闲注入不改变原版行为；
- 文档足以让新模块只使用公共 API 开发。

### 行为级完整逆向完成

在上述基础上，还必须：

- 主要 Tick 顺序已知；
- 主要计算公式有对照测试；
- 战斗、补给和 AI 的关键决策可解释；
- 固定输入可以得到与原版一致或在规定误差内一致的输出；
- 存档和对象状态转移有完整规范。

### 原始源码恢复

不设此验收目标。我们能够恢复的是行为、类型、数据流、调用接口和兼容实现所需的规范，而不是已经在编译过程中丢失的原始工程信息。
