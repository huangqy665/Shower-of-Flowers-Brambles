# Project Dillen 工程开发备忘录

> 本文是 Project Dillen 当前唯一的总体架构与开发方向文档，用于取代旧的 `Project-Dillen工程开发概览.md`。旧文档只保留历史参考价值，不再作为新增系统、目录划分和验收标准的依据。

## 1. 项目定位和设计原则

### 1.1 项目定位

Project Dillen 是一个面向大战略游戏的**机制可定义、加载时编译的通用运行时与 HOI3 兼容重实现工程**。

Project Dillen 的核心目标，**不是在 C++ 引擎内部持续固化和扩展特定的 Gameplay System，而是构建一个机制可定义的通用大战略运行时**。外部规则包或 Mod 可以声明机制的数据结构、运行状态、行为算法；Parser Frontend 负责发现、分类并解析这些外部定义及其实例，Dillen Kernel 则负责将其实例化到权威世界，并提供统一的状态存储、生命周期管理、算法执行、调度、事件交互和持久化能力。HOI3 的数据模型、脚本语义以及国家、战争、外交、科技、生产等既有玩法规则，**是 Project Dillen 的首套兼容目标与架构验证样本，而不是 Dillen Kernel 预先规定的封闭玩法集合**。Dillen 的长期目标是**在保持权威状态、确定性和高性能执行的前提下，使新的 Gameplay Mechanism 能够通过外部定义与注册进入运行时，而原则上无需修改 Kernel 的具体玩法代码**。

Project Dillen 的最终产品由**两个架构解耦但相互组合的核心层构成：**

1. **Dillen 通用运行平台（Dillen Runtime Platform）:**
负责提供与具体 Gameplay Mechanism 业务语义无关的通用运行能力，包括机制类型与实例管理、权威世界及状态存储、算法执行、生命周期与调度、查询与命令、事件分发、引用关系管理以及持久化等基础设施。Dillen Runtime Platform 只规定机制如何被描述、实例化、存储、访问、执行、交互和持久化以及其必须遵守的通用运行时契约。在进入 Simulation 前，仅依据当前 Ruleset 声明的必需项，对 Mechanism、Schema、Algorithm、Runtime Capability 及其依赖关系执行完整性与可运行性验证。具体 Ruleset 要求哪些 Gameplay Mechanism 存在，由 Ruleset 自身声明，而不由 Kernel 预设。HOI3 的既有机制集合因此不会被固化为 Dillen Kernel 的封闭业务模型。
2. **HOI3 兼容实现包（HOI3 Compatibility Package）：**
负责理解并兼容 HOI3 / TFH 及其 Mod 所使用的数据格式、资源组织方式、脚本语义和游戏规则，将地图、场景历史、国家、单位、科技、事件、决议、界面资源及其他 HOI3 内容解析、转换并**将原有的HOI3游戏语义编译为 Dillen Kernel 可运行的模板、算法、定义和实例，从而在不将 HOI3 特有玩法逻辑硬编码进 Kernel 的前提下重建原版游戏行为。**该兼容层负责保存 HOI3 特有的格式知识与兼容语义；其输出可以表现为机制定义、模板、算法、静态定义、场景实例或其他 Dillen Runtime Representation。**HOI3 特有的兼容逻辑绝对不得反向侵入 Dillen Kernel。**\
**现有基于 hoi3_tfh.exe 的进程内 Hook、Native Effect、Native Query、Script GUI、Reverse Probe 以及相关差分观测设施继续保留，作为 HOI3 Reference / Oracle Platform。该平台负责观测原版运行时状态、验证行为语义、构造针对性实验、获取兼容所需的行为契约，并为 Dillen 的实现结果提供差分验证依据。该平台属于兼容研究与验证工具链，而不是 Dillen 最终独立运行时的组成部分或运行依赖。当 Dillen 对某项 HOI3 行为语义存在不确定性时，应通过 Reference / Oracle Platform 获取可验证的原版行为结果，再将得到的兼容语义独立实现于 HOI3 Compatibility Package 或相应 Runtime Capability 中。观测平台基于研究继续保留，但不再代表最终独立引擎的总体架构。**

### 1.2 Dillen Kernel 的职责

Dillen Kernel 是 Parser 解析结果的通用运行平台。Kernel 不编码具体 Gameplay Mechanism 的业务语义，不需要理解“经济”“选举”“殖民”“移民”“国家计划”等机制在游戏规则层面的含义，只定义并管理机制在运行时必须遵循的统一结构与行为契约：

- 机制具有稳定的类型标识、定义标识与实例身份；
- 机制实例的数据结构必须符合已注册 Schema，并在进入运行时前完成校验与解析；
- 机制实例可以通过稳定引用关联世界实体、定义对象或其他机制实例；
- 机制具有明确且可调度的生命周期，包括创建、启用、Tick、暂停、恢复、结束与销毁；
- 机制通过 Query 读取权威状态，通过 Command 请求状态变更，通过 Event 接收或发布运行时通知；
- 机制运行状态必须能够被确定性序列化与恢复，并支持 Schema 或版本演进所要求的状态迁移；
- 机制不得绕过 Kernel 与 Authoritative World 自行维护与权威状态冲突的第二套事实来源；
- 机制之间原则上通过 Kernel 提供的通用运行时接口交互，而不直接依赖彼此的内部存储布局或实现细节。

Kernel 负责机制“如何存在、如何保存、如何运行、如何与世界交互”，但不负责决定机制“在游戏规则上代表什么”。

### 1.3 模板、算法与实例分离

Project Dillen 将一个 Gameplay Mechanism 的结构描述、行为实现、静态定义和运行状态严格分离：

| 层级                            | 负责内容  | 不负责内容                                                 |
| ----------------------------- | -------------------------------------------------------------------------- | ------------------------------------------------------------ |
| **机制模板 Mechanism Template**   | 声明机制可接受的数据结构，包括字段类型、默认值、必填性、引用类型、约束、索引需求、持久化属性以及其他 Schema 元数据              | 不包含具体机制内容，不执行业务行为，不持有战局状态                                    |
| **机制算法 Mechanism Algorithm**  | 实现机制的业务算法，包括 Tick 计算、事件处理、命令处理、业务条件判断、状态转换请求以及派生值计算；算法由 Kernel 在规定的运行阶段和执行上下文中调用，不拥有机制生命周期控制权。 | 不决定外部文件的物理组织方式，不直接定义实例存储布局，不拥有独立权威状态                         |
| **机制定义 Mechanism Definition** | 表示经 Parser 按 Template 校验和编译后的静态机制内容，是外部 Ruleset / Mod 对某类机制的具体声明           | 不表示某次战局中的可变运行状态，不自行执行算法                                      |
| **机制实例 Mechanism Instance**   | 表示某次战局中由 Definition 实例化产生的权威运行状态，包括实例身份、可变字段、实体或实例引用，以及由 Schema 声明的持久化算法状态 | 不重新定义 Template 或 Algorithm，不维护脱离 Authoritative World 的第二套事实源 |

**Parser 根据 Mechanism Template 对外部数据进行解析、类型检查与校验，并生成 Mechanism Definition；实例化层根据 Definition 在 Authoritative World 中创建 Mechanism Instance；Dillen Kernel 负责调度算法并维护实例的完整生命周期。**

### 1.4 设计原则

0. **规则集完整性验证**：Dillen 在进入 Authoritative World 构建和 Simulation 之前，必须完成当前 Ruleset 的完整性、依赖闭包和运行能力验证。任何被 Ruleset.txt 声明为必需的 Mechanism、Schema、Algorithm、资源或 Runtime Capability 缺失、无效或版本不兼容时，启动流程必须在进入游戏前失败，并产生可定位诊断。必需机制集合由 Ruleset 声明，不由 Kernel 硬编码。
1. **机制外置**：新增普通 Gameplay Mechanism 原则上只需要增加或修改 Template、Algorithm、Definition/Data、GUI 与 Localization，而不修改 Dillen Kernel 的具体 Gameplay C++ 代码。只有现有 Runtime 缺少该机制所必需的通用 Capability 时，才需要扩展 Kernel。
2. **Parser 与运行时分离**：Parser 只生成 Definition 和诊断，不执行机制算法，也不直接修改权威世界。
3. **数据结构与行为分离**：Mechanism Template 定义数据结构与约束；Mechanism Definition 承载静态规则内容；Mechanism Instance 承载战局中的权威运行状态；Mechanism Algorithm 定义业务行为；Dillen Kernel 掌握实例生命周期、调度与执行权。
4. **权威世界唯一**：国家、省份、单位、机制和关系及其他可变游戏状态只能有一个权威状态来源；索引和缓存必须可由Authoritative State重建。
5. **稳定身份**：Definition、Entity、Mechanism Instance、Algorithm 和资源均使用稳定 ID，不以进程指针或容器下标作为存档身份。
6. **确定性优先**：同一输入、命令序列、随机种子和版本必须得到相同结果；容器遍历、事件排序和 Tick 顺序必须明确。
7. **事务修改**：机制不能直接任意写内存，所有世界修改通过受控 Command/Transaction 完成，失败时不得留下半更新状态。
8. **能力而非名称耦合**：Mechanism 与其他系统之间应依赖稳定、可声明、可验证的 Capability Contract，而不是依赖具体 Mechanism 名称或其内部实现，例如其他系统依赖 `hostile_relation`、`owns_location`、`consumes_resource` 等 Capability，而不是依赖某个固定机制名称。提供同一 Capability Contract 的不同机制原则上应具有可替换性。
9. **兼容层隔离**：HOI3 特有路径、拼写、历史容错和语义映射留在 `parsers/hoi3` 与兼容包中，绝对不进入 Kernel。
10. **可诊断失败**：未知字段、版本不匹配、悬空引用、算法缺失和迁移失败必须产生可定位诊断；对于影响权威状态正确性的错误，不能静默忽略后继续运行。
11. **定义动态、运行静态**：外部定义可以灵活，但进入运行时前必须编译为稳定布局、索引和执行计划；最终 Tick 不应长期依赖无类型字符串字典。
12. **原生扩展是可选加速层**：只有新增 Kernel 原语、底层渲染后端或高性能算法时才需要 C++；普通机制不以原生插件为前提。

### 1.5 实现边界

“引擎不理解业务含义”不等于“引擎没有规则”。以下内容必须属于 Kernel：

- ID、类型、Schema、引用和版本系统；
- 世界时钟、确定性调度和随机数流；
- Entity 与 Mechanism Instance 的生命周期；
- Command、Event、Query、Capability 和 Transaction 协议；
- 权威状态存储、索引重建、存档和迁移；
- 算法沙箱、资源预算、错误隔离和诊断；
- 渲染、音频、输入、文件和线程等平台服务抽象。

如果一个新机制只能通过增加 C++ `RuntimeXXXState`、`BuildXXXGraph()` 和专用 Setter 才能成立，说明它尚未进入通用机制体系。只有无法由现有 Kernel 原语表达的新基础能力，才允许扩展 Kernel。

### 1.6 最小验收标准

通用机制系统完成的最低标准是：

1. 在不修改 Kernel C++ 的前提下注册一种新机制模板；
2. Parser 能依据模板解析机制定义并完成类型、范围和引用校验；
3. WorldBuilder 能把定义实例化到 Authoritative World；
4. 算法能响应 Tick、事件和命令，并通过事务修改世界；
5. GUI 能通过 Query 读取状态并通过 Command 请求操作；
6. 机制实例能够保存、读档和执行 Schema 版本迁移；
7. 缺少模板、算法或依赖时只隔离对应机制，不破坏整个世界；
8. 同一内容与命令序列可以通过确定性回归测试。

战争系统应成为第一套迁移到该模型的完整验收样本，而不是继续作为永久硬编码的特例。

## 2. Project Dillen 的系统架构

### 2.1 总体数据流

```text
Mod / HOI3 / Dillen Content Packages
                │
                ▼
      VFS + FileCatalog + Source Layer
                │
                ▼
 Template Dispatch + Parser Registry + Lexer/Parser
                │
                ▼
       Analyzer Declare / Resolve / Validate
                │
                ▼
 Schema Registry + Algorithm Registry + Definition Registry
                │
                ▼
             WorldBuilder
                │
                ▼
          Authoritative World
   ┌────────────┼─────────────┐
   ▼            ▼             ▼
Scheduler   Command/Event   Query/Capability
   │            │             │
   └──────── Mechanism Runtime ┘
                │
       Persistence / Migration
                │
      GUI / AI / Audio / Tools
```

### 2.2 内容包层

内容包是机制、数据和表现资源的发布单位。一个内容包可以包含：

```text
manifest
templates/
algorithms/
data/
history/
gui/
localisation/
assets/
migrations/
tests/
```

内容包通过 Manifest 声明包 ID、版本、依赖、加载顺序、兼容范围和可选 Capability。原版 HOI3、TFH、Mod 和纯 Dillen 扩展都应被转换为同一种来源层模型，而不是在 Kernel 内写死目录优先级。

### 2.3 输入前端层

输入前端负责把不同文件方言转换为 Definition IR：

- Clausewitz 文本、CSV、Lua 数据、位图、二进制地图和资源清单使用不同 Reader；
- Template Dispatch 根据虚拟路径、文件类型和内容包声明选择 Parser；
- Parser Registry 管理 Parser 实现，不让 Analyzer 依赖具体文件类型；
- Analyzer 统一执行 Declare、Resolve、Validate；
- Diagnostic 系统保存来源层、虚拟路径、行列、错误码和恢复策略；
- Parser 输出不可变 Definition，不创建运行时对象。

Parser 是 Dillen Kernel 的输入前端，不是机制调度器、脚本虚拟机或权威状态容器。

### 2.4 定义层

定义层位于 Parser 与 Kernel 之间，至少包含：

- `SchemaRegistry`：注册模板、字段、角色、约束和版本；
- `AlgorithmRegistry`：注册算法 ID、入口点、权限、依赖和确定性声明；
- `DefinitionRegistry`：保存已解析、已 Resolve、已冻结的实体与机制定义；
- `CapabilityRegistry`：声明定义或算法能够提供和需要的通用能力；
- `ResourceRegistry`：管理纹理、字体、模型、音频和本地化等稳定资源 ID；
- `MigrationRegistry`：注册 Schema 与存档状态的版本迁移路径。

定义层只描述“可以创建什么”，不保存“当前战局中已经发生了什么”。

### 2.5 Dillen Kernel

Dillen Kernel 是独立引擎的最小可信核心，由以下服务构成：

- 类型与稳定 ID 服务；
- Entity Registry；
- Mechanism Instance Store；
- Lifecycle Manager；
- Deterministic Scheduler；
- Command Bus 与 Transaction Manager；
- Event Bus；
- Query Service；
- Capability Resolver；
- Algorithm Runtime；
- Persistence 与 Migration；
- Diagnostics、Budget 和 Fault Isolation。

Kernel 不直接包含 WarSystem、ElectionSystem 或 MigrationSystem。它只提供足以让这些系统以外部机制存在的公共原语。

### 2.6 权威世界层

Authoritative World 保存当前战局唯一有效的状态：

- 当前世界时间、随机数流和全局调度序列；
- Entity 的稳定身份、类型和组件；
- Mechanism Instance 的模板、算法、状态和参与者引用；
- 已提交命令产生的事实状态；
- 待处理事件、延迟任务和必要的算法状态；
- 可重建索引与只读派生缓存。

WorldBuilder 只在加载场景、创建新战局或重建快照时把 Definition 编译为 Authoritative World。游戏运行后，世界变化由 Kernel Command 和机制算法驱动，而不是重新执行 Parser。

### 2.7 算法运行层

机制算法可以具有三种实现后端：

1. **声明式算法/字节码**：优先用于普通规则、条件、效果和调度逻辑；
2. **受控脚本运行时**：用于复杂但不要求原生性能的机制，可由 Lua 或未来 Dillen Script 承担；
3. **原生算法插件**：用于路径搜索、地图运算、大规模 AI 等性能关键算法，但必须实现统一 Algorithm ABI。

三种后端必须使用相同的 Query、Command、Event 和 Capability 协议。原生算法不能获得绕过事务直接写 Authoritative World 的特权。

### 2.8 表现与交互层

GUI、AI 决策器、音频、调试器和外部工具都是 Kernel 客户端：

- 通过 Query 获取只读快照；
- 通过 Command 提交玩家或 AI 意图；
- 通过 Event 订阅变化；
- 不直接持有世界对象裸指针；
- 不把显示状态当作权威玩法状态。

现有 Script GUI 可作为第一套声明式表现前端，但需要逐步把 HOI3 进程内桥接替换为 Dillen Query/Command/Event 接口。

### 2.9 HOI3 兼容层

HOI3 兼容层负责：

- 识别原版、TFH 和 Mod 的 VFS 覆盖规则；
- 解析 HOI3 文件方言和历史容错；
- 把国家、省份、单位、科技、外交、战争、事件等语义映射为 Dillen Definition；
- 提供与原版行为一致的机制模板和算法；
- 使用原版进程观测平台进行 Golden Trace 与差分验证。

兼容层可以知道 HOI3 的业务含义，Kernel 不知道。HOI3 兼容实现只是运行在 Kernel 上的一组内容、算法和适配器。

### 2.10 观测与注入层

现有 `core/engine/native/hoi3`、Hook、Native Effect、Native Query、SaveLoaded Barrier 和 Reverse Probe Framework 继续承担：

- 验证原版 HOI3 行为；
- 定位不确定语义；
- 生成差分样本；
- 为兼容算法建立 Golden Trace；
- 在独立引擎尚未覆盖某功能时提供临时实验环境。

该层原则上冻结横向扩展，只按兼容重实现中的具体问题增加 Probe。它不是 Authoritative World，也不应成为新机制的长期运行后端。

## 3. 各系统的具体构成、职责与实现边界

### 3.1 VFS 与 FileCatalog

**具体构成**：Source Layer、虚拟路径、覆盖优先级、`replace_path`、文件编码、来源追踪、内容包依赖。

**负责工作**：组合原版、资料片、Mod 和 Dillen 内容包；为每个活动文件确定唯一来源；向 Parser 提供稳定虚拟路径和原始字节。

**实现边界**：不解析业务字段，不创建 Definition，不决定机制行为。编码转换不得无提示覆盖原文件。

### 3.2 Template Dispatch 与 Parser Registry

**当前基础**：`template_registry`、`parser_registry`、`lexer`、`parser_cursor`、`path_pattern` 和 SourceBuffer 已经形成可用文本前端。

**负责工作**：

- 根据路径和 Dialect 选择 Parser；
- 把输入转换为类型化 Parse Artifact；
- 保留重复字段、源顺序、动态键和 Source Span；
- 允许 Generated Parser 与手写 Parser 共存。

**实现边界**：Parser 不查询运行时世界，不执行 Effect，不调度 Tick，不保存当前战局状态。

### 3.3 Analyzer

**当前基础**：已有 Declare、Resolve、Validate 三阶段和可注册 Analysis Pass。

**负责工作**：

- Declare 稳定符号与 ID；
- Resolve 跨文件、跨来源层和跨 Registry 引用；
- Validate 类型、范围、互斥项、依赖和全局一致性；
- 产生完整诊断并阻止无效 Definition 冻结。

**实现边界**：Analyzer 可以构建 Definition 间引用，但不能实例化 Mechanism Instance，也不能根据当前日期运行机制。

### 3.4 Mechanism Schema Registry

**目标构成**：

```text
MechanismTypeId
SchemaVersion
FieldSchema
RoleSchema
ReferenceSchema
ConstraintSchema
IndexSchema
PersistenceSchema
CapabilityDeclaration
```

**负责工作**：定义机制数据结构、字段默认值、角色基数、允许引用、范围约束、索引需求和持久化规则。

**实现边界**：Schema 不包含业务执行代码。复杂行为必须引用 Algorithm ID，不允许把任意 C++ 回调藏入字段验证器。

**当前实现**：`src/kernel` 已建立 `MechanismTypeId`、版本化 `MechanismSchemaRegistry`、字段/角色 Schema 和递归统一值类型。统一值当前覆盖 Null、Boolean、Integer、Decimal、String、稳定 Reference、List 与 Object；Schema 注册阶段校验默认值、数值/长度范围、列表元素类型、引用类型、角色基数、重复字段和版本冲突。

### 3.5 Algorithm Registry 与 Algorithm Runtime

**目标构成**：Algorithm ID、后端类型、入口点、输入输出、权限、依赖 Capability、状态布局、Tick 阶段和预算。

**负责工作**：加载算法、验证 ABI、建立执行计划、调用生命周期入口、限制耗时和内存、隔离失败算法。

**实现边界**：算法只能使用 Kernel API；不得访问容器内部地址、渲染设备或兼容层原生指针。算法需要的新公共能力必须先抽象为 Query、Command 或 Capability。

**当前实现**：已完成版本化 `AlgorithmRegistry` 和 `AlgorithmDescriptor`，登记稳定 Algorithm ID、Backend、生命周期入口点、确定性声明与所需 Capability。当前 Registry 只登记和验证算法元数据，尚未加载字节码、脚本或原生 Algorithm ABI，也尚未执行算法。

### 3.6 Mechanism Definition Registry

**目标对象**：

```text
MechanismDefinitionId
MechanismTypeId
SchemaVersion
AlgorithmId
DefinitionFieldValues
StaticRoleBindings
SourceOrigin
DependencySet
```

**负责工作**：保存 Parser 编译后的机制定义，执行稳定排序和冻结，为 WorldBuilder 提供只读输入。

**实现边界**：Definition 是共享静态数据，不保存某个存档中的进度、冷却、参与者变化或临时结果。

当前 `DefinitionRegistry` 已聚合 Country、Province、Technology、OOB、DiplomacyHistory 和 WarHistory 等强类型 Registry。这些实现是建立通用 Mechanism Definition Registry 的参考切片，不应继续无限增加并列的业务 Registry。

`src/kernel` 已新增通用 `MechanismDefinitionRegistry`。Definition 通过 `MechanismDefinitionId + MechanismTypeId + SchemaVersion + AlgorithmId/Version` 绑定结构与行为，字段和角色在声明阶段依据已冻结 Schema 校验，并自动物化 Schema 默认值。Registry 冻结后按稳定 ID 排序并只读查询；它尚未接入 Parser Artifact，但已可作为 WorldBuilder 创建 Mechanism Instance 的冻结输入。

### 3.7 WorldBuilder

**负责工作**：

- 选择 Bookmark、Scenario 和开始日期；
- 从冻结 Definition 创建 Entity 与 Mechanism Instance；
- 回放开始日期之前的历史 Patch；
- 建立双向引用和可重建索引；
- 在临时候选世界完成校验后原子发布。

**实现边界**：WorldBuilder 只负责初始实例化，不承担每日 Tick，不执行玩家命令，也不长期拥有业务专用 Graph Builder。

当前 `CountryState`、`ProvinceState`、`RuntimeUnitState`、`CountryRelationState`、`RuntimeWarState` 和对应 `BuildXXXGraph()` 属于过渡期强类型模型。后续应把战争作为首个 `MechanismInstance` 迁移样本，再根据结果迁移外交关系和其他可机制化状态。

WorldBuilder 已增加接受冻结 `MechanismDefinitionRegistry` 的构建入口。构建过程先在临时候选世界中完成既有强类型状态和全部初始机制实例，任一步失败都不会替换已发布世界；旧入口继续保留，并产生空 Mechanism Instance Store。当前最小实例化策略是每个 Mechanism Definition 在世界创建时生成一个序号为 0、创建 Tick 为 0 的实例，动态生成策略留给后续生命周期与命令系统。

### 3.8 Authoritative World 与 Entity Registry

**目标构成**：Entity ID、Entity Type、Component/Property Store、Mechanism Instance Store、Relation Index、World Clock、RNG Streams、Event Queue。

**负责工作**：保存战局唯一事实源，提供稳定查询，维护引用完整性，并支持 Snapshot、Rollback 和存档。

**实现边界**：

- Entity 不因 GUI 关闭而销毁；
- 派生索引不能反向成为事实源；
- 外部模块不能取得可长期保存的对象地址；
- 世界更新只能发生在事务提交点；
- HOI3 的 Country/Province/Unit 语义应由兼容 Schema 与 Capability 表达，而不是扩散到 Kernel 公共 API。

**当前实现**：`AuthoritativeWorld` 已直接拥有 `MechanismInstanceStore`、`WorldCommandQueue`、`WorldEventQueue`、`MechanismScheduler` 和最新 `MechanismQuerySnapshot`，并记录 World Tick 与 Revision。Store 随候选世界一起原子发布，不使用进程指针、GUI 状态或外部全局表作为机制事实源；运行期写入通过世界事务或 Scheduler 提交，普通查询只读取已发布快照。

### 3.9 Mechanism Instance Store

**目标对象**：

```text
MechanismInstanceId
MechanismDefinitionId
MechanismTypeId
LifecycleState
DynamicFieldValues
RoleBindings
AlgorithmState
CreatedTick / UpdatedTick
```

**负责工作**：创建、查询、启停、结束和销毁实例；维护实例到 Entity、Definition、Algorithm 和 Capability 的索引。

**实现边界**：实例动态字段必须符合 Schema；算法私有状态也必须可序列化。不得通过额外全局表保存 Kernel 不知道的权威业务状态。

**当前实现**：`src/kernel/mechanism_instance.hpp` 定义了实例身份、Definition/Type/Algorithm 绑定、Schema 版本、生命周期状态、动态值、角色绑定、算法状态以及创建/更新时间；`mechanism_instance_store.hpp/.cpp` 实现了创建、清空、按实例 ID 查询、按 Definition 查询、按 Type 查询和确定性遍历。实例 ID 由 `MechanismDefinitionId + Definition 内创建序号` 稳定生成，因此新增无关 Definition 不会改变既有实例的身份序列。创建时仅接受已冻结 Definition Registry，并从已校验 Definition 复制默认值、初始字段和角色绑定。

Store 已进一步接入 Lifecycle、Command 与 Transaction 基础，可在冻结 Definition/Schema 约束下原子更新动态字段和生命周期。当前仍不实现实例销毁、角色引用重绑定、算法私有状态写入、动态创建命令、存档恢复和 Schema 迁移；这些能力必须沿用同一事务边界，不能通过临时可变接口绕过权威世界。

### 3.10 Scheduler、Lifecycle 与时间

**负责工作**：定义 Tick 阶段、算法优先级、事件顺序、延迟任务、暂停、倍速和预算；保证同一 Tick 内的执行顺序可复现。

建议的最小阶段为：

```text
Collect Commands
→ Validate Commands
→ Apply Transactions
→ Dispatch Events
→ Run Scheduled Mechanisms
→ Rebuild Dirty Indexes
→ Publish Read Snapshot
→ Persistence Barrier
```

**实现边界**：机制不能自行创建线程推进权威状态；异步任务只能计算候选结果，最终提交必须回到确定性阶段。

**当前实现**：`mechanism_lifecycle.hpp/.cpp` 已定义 `Created → Active/Completed/Failed`、`Active ↔ Paused` 以及 Active/Paused 到 Completed/Failed 的确定性状态转换；Completed 与 Failed 为不可恢复终态，同状态转换为合法无操作。生命周期变化只能作为 `MechanismCommand` 进入事务，不能取得可写实例后直接改值。

`mechanism_scheduler.hpp/.cpp` 已建立基础 Tick 提交管线：只接受严格递增的下一 Tick，在 Definition/Schema Registry 冻结屏障通过后按队列序号处理本 Tick 可执行的 World Transaction，记录提交或拒绝结果，发布事实事件，并在 Tick 末发布一致 Query Snapshot。Registry 或 Tick 屏障失败时不会取走排队命令。当前 Scheduler 尚未执行 Algorithm Registry 的 Create/Tick/Event 入口，也未实现优先级、预算、延迟算法任务、事件订阅分发和多阶段脏索引重建。

### 3.11 Command、Transaction、Event 与 Query

| 服务 | 职责 | 禁止事项 |
|---|---|---|
| Command | 表达玩家、AI、脚本对世界的修改意图 | 不保证提交成功，不直接返回可写对象 |
| Transaction | 校验并原子提交一组状态变化 | 不允许部分提交后继续运行 |
| Event | 发布已经发生的事实与生命周期通知 | 不作为另一套隐式命令通道 |
| Query | 读取一致的世界快照和派生数据 | 不暴露可写引用 |
| Capability | 解耦机制间依赖，描述可提供的通用能力 | 不以具体 Mod 名或机制显示名作为协议 |

GUI、AI、机制算法和调试工具必须共享这套交互模型。

**当前实现**：`mechanism_command.hpp/.cpp` 已提供与业务无关的 `SetField` 和 `TransitionLifecycle` 命令；`mechanism_transaction.hpp/.cpp` 定义统一提交结果、失败命令下标、目标实例、变更实例数和字段/生命周期变更记录；`MechanismInstanceStore::ApplyTransaction()` 按命令顺序在临时实例副本上执行完整校验，全部成功后才提交变更。事务会验证 Definition/Schema Registry 冻结状态、实例与 Definition 元数据一致性、字段存在性、值类型与范围、生命周期转换和 Tick 单调性，失败时不写入任何实例。无实际变化的合法事务不会推进 `UpdatedTick`。

`world_transaction.hpp/.cpp` 已定义可扩展的 `WorldCommand`/`WorldTransaction` 信封；当前首个命令变体是 `MechanismCommand`，一组命令作为一个世界事务原子提交。后续增加 Entity、Relation、资源或外交命令时，应扩展 WorldCommand 变体并在统一事务暂存区提交，不能再增加平行 Setter。

`world_command_queue.hpp/.cpp` 以单调 Sequence 和 `notBeforeTick` 保存事务，Scheduler 只取走当前 Tick 已到期的事务，延迟事务保持原顺序。`world_event.hpp/.cpp` 为每个事务发布 Committed/Rejected 事实，并在成功后按命令顺序发布字段与生命周期变化；事件拥有独立单调 Sequence，可由消费者 Drain，但不能反向修改世界。

`mechanism_query_snapshot.hpp/.cpp` 在 WorldBuilder 完成和每个 Scheduler Tick 末复制发布只读实例视图，包含 Tick、Revision、实例 ID、Definition 与 Type 索引。旧 Snapshot 在新事务提交后保持不变，GUI、AI 和算法查询应逐步改为读取 Snapshot，而不是读取可变化的运行容器。

对于已发布世界，`AuthoritativeWorld::EnqueueWorldTransaction()`、`RunMechanismSchedulerTick()` 和受控的即时 `ApplyWorldTransaction()` 构成当前写入口；`Mechanisms()`、`WorldCommands()`、`WorldEvents()` 和 `MechanismSnapshot()` 均只公开只读视图。当前世界事务只覆盖 Mechanism Instance Store，尚未把过渡期 Country/Province/Unit/Relation/War 容器纳入同一暂存区；真正跨 Entity 与 Mechanism 的事务原子性将在 Entity Registry 建立后补齐。

### 3.12 Persistence 与 Migration

**必须保存**：世界版本、内容包集合、Schema 版本、Entity、Mechanism Instance、动态字段、算法状态、世界时间、RNG、事件队列和稳定引用。

**必须支持**：

- Definition 与 Instance 分离保存；
- 稳定 ID 重解析；
- Schema 逐版本迁移；
- 内容包缺失和算法缺失的明确诊断；
- 未知机制的隔离、只读保留或拒绝加载策略；
- 存档前后确定性校验和；
- 不保存可重建缓存和进程地址。

**实现边界**：任何无法序列化的算法私有状态都不能进入权威机制。兼容原版 HOI3 存档可以由独立 Importer 处理，不要求 Dillen 内部长期使用原版存档布局。

### 3.13 GUI 与表现系统

**当前基础**：Script GUI 已具备声明式窗口、图片、文字、按钮、列表、滚动条、进度条、自定义控件、数据绑定、动作桥和 D3D9 后端。

**未来职责**：把 GUI Definition 编译为表现树，通过 Query 绑定数据显示，通过 Command 绑定动作，通过 Event 刷新脏数据。

**实现边界**：GUI 不保存机制权威状态，不直接调用业务 Setter，不因渲染帧率改变模拟 Tick。D3D9 只属于 HOI3 注入宿主；独立引擎需要新的渲染后端，但上层 GUI Definition 和交互模型应保持后端无关。

### 3.14 HOI3 Parser 与兼容包

`parser/parsers/hoi3` 当前已经覆盖 Country Tag、Country Definition、Province、Region、Country/Province History、UnitType、Technology、UnitModel、OOB、Diplomacy History、War History 和 Scenario/Bookmark 的首批切片。

这些 Parser 的长期职责是把 HOI3 方言转换为通用 Definition，而不是让每一种 HOI3 业务对象永久拥有一套独立运行时容器。HOI3 特有容错、字段拼写和历史日期规则可以保留在兼容 Parser 中，但输出应逐步迁移到通用 Schema、Mechanism Definition 和 Entity Definition。

### 3.15 观测平台与差分验证

**当前构成**：Module Registry、Hook Registry、Lifecycle、Engine Registry、Capability Registry、Native Query、Native Effect、Object Resolver、SaveLoaded Barrier 和 Reverse Probe Framework。

**负责工作**：针对不确定的原版行为设计最小 Probe，记录输入、原版输出和状态差异，为兼容算法提供可审计证据。

**实现边界**：不继续为了潜在用途横向逆向 Unit、Supply、Combat 和 AI 全套接口；只在独立实现遇到明确语义缺口时扩展。Probe 结果进入测试资料和兼容算法，不能直接成为 Kernel 的隐藏依赖。

### 3.16 测试与验收体系

测试应分为五层：

1. Parser 语法和错误恢复测试；
2. Definition Resolve、Schema 和 Registry 冻结测试；
3. WorldBuilder 初始世界与引用一致性测试；
4. Mechanism 生命周期、命令、事件、存档和确定性测试；
5. HOI3 Golden Trace 差分测试。

每一种机制模板至少提供：合法样本、非法样本、跨文件引用样本、存档迁移样本和固定 Tick 输出样本。性能测试必须区分加载期编译成本与运行期 Tick 成本。

### 3.17 当前目录与目标职责映射

| 当前目录 | 当前职责 | 长期定位 |
|---|---|---|
| `src/kernel` | 机制强类型 ID、统一值、Schema/Algorithm/Definition Registry、Instance Store、Lifecycle、World Transaction、Command Queue、Scheduler、Event Queue 与 Query Snapshot | Dillen Kernel 的通用机制运行基础；后续承载算法执行、Capability、Entity 事务和持久化 |
| `src/parser` | VFS 后的文本前端、Template、Parser、Analyzer | Dillen 通用输入前端 |
| `src/parser/parsers/hoi3` | HOI3 文件语义解析 | HOI3 兼容前端 |
| `src/content` | 强类型 Definition 与 Registry | 逐步拆分为通用 Schema/Definition 和 HOI3 Definition 适配层 |
| `src/worldbuilder` | 强类型世界实例化和一致性校验 | Kernel WorldBuilder 与迁移期兼容 Builder |
| `src/core` | 注入模块、Hook、生命周期和 Capability 基础 | 可复用概念迁入 Kernel；Windows 注入部分留在兼容宿主 |
| `src/engine` | HOI3 版本、符号和类型注册 | 原版观测平台专用 |
| `src/native` | 原生查询、效果、对象解析和存档屏障 | 原版观测与兼容验证专用 |
| `src/hoi3` | HOI3 原生行为桥 | 原版观测平台专用 |
| `src/gui` | Script GUI 模型、数据、运行时、Lua 和 D3D9 | 保留声明式上层，替换为 Kernel Query/Command 与独立渲染后端 |
| `src/leader_capture` | 注入式将领俘虏机制 | 作为行为差分样本，未来重写为外部机制 |
| `src/launcher`、`src/dll` | Windows 启动与注入 | 保留兼容/观测模式；未来启动器增加独立 Dillen 模式 |
| `src/tools`、`tests` | 离线工具与 Probe | 扩展为内容编译器、Schema 工具和确定性测试平台 |

### 3.18 当前阶段的迁移顺序

当前不应继续为每种玩法增加新的专用 `RuntimeXXXState`。建议按以下顺序建设：

1. **已完成基础版**：定义 `MechanismTypeId`、`MechanismDefinitionId`、`MechanismInstanceId`、`AlgorithmId` 和统一值类型；
2. **已完成基础版**：建立版本化 `MechanismSchemaRegistry`、`AlgorithmRegistry` 和通用 `MechanismDefinitionRegistry`；
3. **已完成基础版**：建立 Authoritative World 中的 `MechanismInstanceStore`，实现确定性身份、Definition/Type 索引、只读查询和 WorldBuilder 原子初始实例化；
4. **已完成基础版**：建立最小 Lifecycle、Command、事务化字段更新、World Transaction、Command Queue、Scheduler、Event Queue 和 Query Snapshot；下一步接入 Algorithm 执行、Capability 与 Entity 级世界事务；
5. 将 War History/Runtime War Graph 迁移为第一套模板、算法和实例；
6. 将 Diplomacy Relation 迁移为第二套机制，验证跨机制 Capability；
7. 接入通用存档、Schema Migration 和确定性回归；
8. 让 Script GUI 只通过 Query/Command/Event 使用机制；
9. 在该纵向闭环通过后，再继续解析 Event、Decision、Leader、Production 等 HOI3 语义。

这一顺序的验收重点不是“又支持了多少 HOI3 字段”，而是“新增一种机制是否仍需修改 Kernel C++”。只有当战争机制能够完全通过模板、算法、Definition 和 Instance 运行时，Project Dillen 才真正从 HOI3 专用重实现骨架转变为通用可机制化运行平台。
