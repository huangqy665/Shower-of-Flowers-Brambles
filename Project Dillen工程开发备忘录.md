# Project Dillen 工程开发备忘录

> **权威状态：生效中。**本文是 Project Dillen 唯一的总体架构、系统边界、开发顺序与阶段验收指导。除源码、测试和版本化接口契约所证明的实现事实外，其他开发概览、讨论记录、逆向备忘录和历史设计文档均不具有架构裁决权。若其他文档与本文冲突，以本文为准。

## 0. 文档治理

### 0.1 权威范围

本文负责确定：

- Project Dillen 的产品定位、核心术语和不可破坏的依赖方向；
- Dillen Kernel、Gameplay Package、Ruleset、Content Package、Importer、Mapping Profile 与 Oracle 的职责边界；
- Authoritative World、Parser、Resolver、Runtime Compiler、Algorithm Runtime、Transaction、Persistence 等系统的实现边界；
- 当前阶段的建设顺序、能力范围和验收门禁。

以下资料只具有局部参考价值：

- `Project-Dillen工程开发概览.md`：历史工程概览；
- `developMemo.md`：Shower of Flowers Mod 内容开发记录；
- HOI3 逆向与原生接口备忘录：Oracle 研究资料；
- `Project-Alice-main`：外部架构和实现参考；
- IDE 日志、聊天记录、临时 Probe 输出：开发过程证据。

### 0.2 变更规则

1. 改变 Kernel 边界、权威状态所有权、依赖方向、Ruleset 语义或外部兼容架构时，必须在同一变更中更新本文。
2. “当前实现”只能描述已经进入仓库并通过相应测试的能力；设计目标必须明确标注为“目标”或“尚未实现”。
3. 新系统不得仅凭未来可能需要而进入 Kernel；必须先证明缺失的是跨 Gameplay Domain、跨 Ruleset 可复用的 Runtime Primitive 或 Capability。
4. 开发阶段必须通过前置能力门禁后才能进入下一阶段，不以 Demo 编号、内容数量或界面完成度代替可验证的架构闭环。
5. HOI3 Importer、Mapping Profile 和 Oracle 的进度不得反向改变 Dillen Kernel 的优先级。

---

## 1. 项目定位与设计原则

### 1.1 项目定位

Project Dillen 是一个**面向大战略游戏的机制可定义、加载时编译、确定性运行的通用 Gameplay Runtime 与独立游戏引擎工程**。

Dillen 的核心产品不是某一套固定大战略规则，而是：

1. 一个不知道具体 Gameplay Meaning 的通用 Kernel；
2. 一套能够由外部 Package 声明 Gameplay Concept、Mechanism 和 Algorithm 的 Gameplay Framework；
3. 一个通过 Ruleset 选择、组合、约束和冻结 Gameplay Package 的装配系统；
4. 一个保存唯一权威世界、执行确定性模拟并支持持久化的 Runtime；
5. 一组可选的平台、表现、工具和外部内容接入能力。

政治、外交、经济、科研、生产、军事和战争等概念不属于 Kernel 的内建业务类型。它们由可版本化的 Dillen Mechanism Package 表达，并由某个 Root Ruleset 选择进入一局游戏。

HOI3 / TFH 及其 Mod 对 Dillen 而言是**可选的外部 Content Corpus 与 Resource Format**，不是 Dillen 必须复刻的 Ruleset，也不是 Dillen Gameplay Model 的定义来源。Dillen 可以复用其文本、地图、图片、本地化、历史、单位和科技等资源，但这些资源进入 Dillen 后的最终 Gameplay Meaning 由目标 Dillen Ruleset 和 Mechanism Package 决定。

### 1.2 产品层与职责边界

#### 1.2.1 Dillen Kernel

Dillen Kernel 是与具体 Gameplay Semantic 无关的最小可信运行时核心，负责：

- Runtime Type、Stable ID、Schema 和版本契约；
- Entity、Component、Relation、Mechanism Instance 与 Authoritative World；
- Lifecycle、Algorithm Runtime、Deterministic Scheduler 与 RNG Stream；
- Query、Command、Transaction、Event、Capability；
- Runtime Freeze、引用完整性、Persistence、Migration；
- Diagnostics、Budget、Fault Isolation 和平台服务边界。

Kernel 只理解“对象和机制如何存在、运行、交互、提交和保存”，不理解“战争、国家、外交或科技等具体机制、实例在玩法上是什么”。

#### 1.2.2 Package 是承载单位，不是业务层

**Package 本身只是版本化承载和依赖边界**：它拥有一个 Manifest、一个内容摘要、一组 Source Layer，并声明自己依赖谁。它不是与 Contract、Content 平级的业务概念。谈论"这是 Contract 还是 Package"是范畴错误——正确的问法是"这个 Package 承担哪种角色"。

角色由 Package **暴露什么、依赖什么**决定，共四种：

| 角色 | 拥有 | 明确不拥有 |
| --- | --- | --- |
| **Contract Package** | 公共 ABI：Capability Contract、Query / Command Contract、GUI 可消费的数据与动作契约、跨包共享的 Component / Relation Schema | 任何业务实现。没有 Algorithm，没有 Definition，没有 Spawn |
| **Mechanism Package** | 业务实现：Mechanism Template / Schema、Algorithm、以及对公共 Query / Command / Capability Contract 的实现或消费 | 不定义供他人依赖的公共 ABI（那属于 Contract Package）；不拥有具体世界数据，也不拥有窗口布局和贴图 |
| **Content Package** | 具体世界：Entity / Mechanism Definition、Spawn、历史、初始状态、场景；对表现资源的**引用** | 不定义 Schema、Algorithm 或通用 GUI 行为 |
| **Presentation Package** | 表现实现：窗口与控件布局、数据绑定表达式、动作到公共 Command / Capability 的绑定、地图表现配置、贴图、字体、音频和本地化文本 | 不含 Gameplay Algorithm，不直接写 Authoritative World，不参与确定性 Gameplay 状态 |

关键约束：**两个 Mechanism Package 不得互相依赖。**需要交互时，双方各自依赖同一个 Contract Package——这是 Demo 交付的 `dillen.demo1.contracts_package` 已经验证的形态（见 §3.8）。

GUI 的职责按同一条线切开，避免三处重复拥有：

- **Contract Package** 声明 GUI 可依赖的稳定 **Query / Command / Capability 契约**；
- **Mechanism Package** 只实现或消费这些契约，不声明具体窗口、控件或贴图；
- **Presentation Package** 拥有全部**表现实现**（布局、绑定表达式、动作映射、贴图、字体、音频、本地化）；
- **Content Package** 只**引用**表现资源，不拥有通用 GUI 行为。

表现身份与权威模拟身份必须分离：影响模拟的地图拓扑、Entity / Component / Relation 初始数据进入普通 Package / Source Lock 与 Ruleset Fingerprint；窗口布局、贴图、字体、音频和纯表现地图几何进入独立 Presentation Lock / Presentation Fingerprint。更换纯表现资源不得使既有存档失效；若某个所谓“表现文件”改变了可提交 Command 的业务语义，它就不再是纯 Presentation，必须把对应语义上移到 Contract / Mechanism / Content 层接受普通 Ruleset 身份约束。

**角色不是身份的一部分（重要推论）**：`PackageRole` 只在 `PackageManifest` 上，**不进 `PackageLockEntry`，也不进 Ruleset Fingerprint**。因此：

- 角色约束是**加载期检查**，由严格 Authoring 模式（`requireExplicitPackageRoles`）执行，默认关闭；
- **存档里不含角色信息**，读档时无法回溯验证当初的包角色是否合规——存档校验的是 Package / Source Lock 与 Fingerprint，与角色无关；
- 因此不得把"存档能加载"理解为"角色边界当初被遵守过"。要让角色成为可追溯身份，必须把它纳入 Lock 与 Fingerprint，那是破坏性变更（升 Save 版本 + 迁移）。

**当前实现状态**：`PackageManifest` 已具有 `contract / mechanism / content / presentation` 显式角色，严格 Authoring 模式会拒绝未声明角色、角色越界内容，以及 Mechanism Package 对非 Contract Package 的依赖。已封存的 Demo 0.5 由一个 Contract Package、三个互不依赖的 Mechanism Package 与一个 Content Package 实际承担并通过 Package Lock、Source Lock 和非法包门禁。地图 / Presentation 纵向切片（原 Demo 0.8）已建立 Presentation Schema / Registry / Compiler、Frozen Presentation Catalog、独立 Presentation Fingerprint、窗口后端、Map Widget、稳定 Entity 映射和 Contract 驱动动作绑定；表现运行期不再解释可编辑控件字符串，也不得通过在 Mechanism 或 Host C++ 中硬编码业务动作来绕过公共契约。

Project Dillen 可以提供一套 **Reference Gameplay Library** 作为官方示例和默认发行内容，但该 Library 不是 Kernel，也不是所有 Ruleset 的强制基础。删除 Reference Gameplay Library 后，Kernel 仍必须能够装载其他完全不同的 Gameplay Package。

#### 1.2.3 Root Ruleset 与 Extension Ruleset

每次启动必须明确选择一个 **Root Ruleset**。Root Ruleset 声明本次 Simulation 的最低 Gameplay Contract、必需 Package、允许的扩展点、覆盖策略和入口场景。

Root Ruleset 不是 Kernel 内部不可替换的“唯一 Core Ruleset”。发行版可以提供受保护的官方 Root Ruleset；Mod 可以在该 Root Ruleset 允许的范围内加载 Extension Ruleset。需要彻底改变玩法时，作者可以提供新的 Root Ruleset，而不是被迫修改 Kernel 或绕过一个全局不可修改的规则集。

Ruleset 决定装配哪些 Package 以及它们的角色组合（见 §1.2.2）；**Package 自身不拥有运行时调度权**。Ruleset 也不是 Package——它是装配声明，选择 Package 并固定其版本范围。

#### 1.2.4 External Corpus Importer / Adapter

External Corpus Importer 是独立于 Kernel、Gameplay Package 和 Ruleset 的**来源规范化工具**。HOI3 Importer 是其中一个实现。

HOI3 Importer 只负责：

- 识别 HOI3、TFH 与 Mod 的目录、资料片、`replace_path` 和覆盖规则；
- 解码文件编码和资源格式；
- 解析 Clausewitz 文本、CSV、Lua 数据、地图、图片、本地化、GUI 和其他源格式；
- 保留重复字段、源顺序、动态键、日期块、引用文本和 Source Span；
- 将输入规范化为稳定、版本化、来源可追踪的 **Normalized HOI3 Source IR**；
- 对源格式错误、损坏资源和无法规范化的结构产生诊断。

HOI3 Importer **不得**：

- 引用某个 Dillen Ruleset 的 Entity、Mechanism、Capability 或 Algorithm ID；
- 决定 HOI3 字段应映射到哪种 Dillen Gameplay Concept；
- 生成目标 Ruleset 专属的 Entity Definition、Mechanism Definition 或 Spawn Definition；
- 定义或执行战争、外交、科技等 Gameplay Algorithm；
- 以 `hoi3_tfh.exe` 的隐藏执行方式作为 Dillen Runtime 契约。

Importer 的验收标准是来源规范化的确定性、完整性、可追踪性和尽可能无损，而不是 Gameplay 等价性。

#### 1.2.5 Mapping Profile

Mapping Profile 是 **Normalized External Source IR 与目标 Dillen Semantic Space 之间的独立声明式投影契约**。

HOI3 Mapping Profile 只负责：

- 声明某类 Normalized HOI3 IR 节点应投影到哪些目标 Entity、Component、Relation、Mechanism Definition、Spawn、资源或历史 Patch；
- 引用目标 Root Ruleset 已装配 Package 暴露的稳定 Gameplay Contract；
- 声明字段转换、枚举对应、单位换算、默认值、过滤、合并和缺失语义策略；
- 生成带来源映射的 **Dillen Projection Artifact**；
- 对未映射、歧义、目标 Contract 缺失或版本不兼容产生明确诊断。

Mapping Profile **不得**：

- 重新读取或私自解析 HOI3 原始文件；
- 把源格式容错和 VFS 覆盖规则复制进映射配置；
- 定义目标 Gameplay Mechanism 的 Schema 或业务 Algorithm；
- 在目标 Package 未公开 Contract 时访问其私有状态；
- 绕过 Resolver、Ruleset Integrity Validation 或 Runtime Compiler 直接创建运行时对象。

同一份 Normalized HOI3 Source IR 可以使用不同 Mapping Profile 投影到不同 Ruleset；同一 Mapping Profile 也必须明确声明兼容的目标 Contract 与版本范围。

#### 1.2.6 HOI3 Oracle

`hoi3oracle` 是冻结式、按需启用的研究和注入平台。它可以为 HOI3 源格式、内容作者实际依赖的可观察语义和模糊字段提供最小 Probe，但不是 Importer、Mapping Profile 或 Dillen Runtime 的运行依赖。

### 1.3 Dillen Kernel 的职责

Kernel 至少提供以下通用能力：

- **Stable Identity**：所有权威引用使用命名空间限定、版本化、可持久化的稳定 ID；
- **Runtime Schema**：进入 Authoritative World 的结构必须符合冻结 Schema 和引用约束；
- **Authoritative World**：所有影响 Simulation 的可变事实只有一个权威来源；
- **Entity / Component / Relation**：提供通用对象、属性和关系的创建、修改、索引和持久化；
- **Mechanism Instance**：保存机制实例身份、字段、角色、生命周期和算法状态；
- **Lifecycle**：统一管理 Created、Active、Paused、Completed、Failed、Destroy 等状态和转换；
- **Algorithm Runtime**：受控调用算法入口点，限制权限、预算和副作用；
- **Query / Command / Transaction**：只读查询、修改意图和跨 Store 原子提交；
- **Event / Scheduled Inbox**：区分已发生事实与未来权威行为；
- **Deterministic Scheduler / RNG**：固定 Tick、Phase、顺序和随机流；
- **Capability Contract**：注册、解析和绑定跨机制通用能力；
- **Persistence / Migration**：保存权威状态并执行版本迁移；
- **Diagnostics / Fault Isolation**：在启动期和运行期提供可定位、可分级的失败策略。

Kernel 不直接解释外部 Corpus，不直接消费 HOI3 IR，也不编码任何特定 Ruleset 的 Gameplay Meaning。

### 1.4 模板、算法、定义与实例分离

| 层级 | 负责内容 | 不负责内容 |
|---|---|---|
| Mechanism Template Source | 可编辑字段、角色、引用、约束、索引和持久化声明 | 不直接成为运行时布局，不保存战局状态 |
| Mechanism Schema | 经 Resolver 验证和版本冻结的类型契约 | 不包含 Gameplay Content，不执行行为 |
| Mechanism Algorithm | Tick、Event、Command、条件和变更请求 | 不直接写 World，不拥有权威状态 |
| Mechanism Definition | 针对 Schema 的共享静态参数、绑定和配置 | 不代表当前战局进度，不自行创建实例 |
| Mechanism Spawn Definition | 场景初始实例化意图 | 不拥有运行时 Instance ID，不保存后续状态 |
| Mechanism Instance | 当前 Simulation 中的权威动态状态 | 不重新定义 Schema、Definition 或 Algorithm |

世界对象必须按身份和生命周期分类：

- **Entity**：具有稳定身份、被多个机制长期引用的对象；
- **Component**：附着于 Entity 的属性集合；
- **Relation**：没有独立算法和生命周期的结构化事实关系；
- **Mechanism Instance**：具有独立状态、算法、事件处理或生命周期的过程；
- **Definition**：共享只读内容。

### 1.5 设计原则

1. **Kernel 业务无知**：Kernel 不得内建 Country、War、Technology 等 Gameplay Type。
2. **机制外置**：新增普通机制原则上只增加 Package 内容，不修改 Kernel C++。
3. **Root Ruleset 可替换**：引擎不强制唯一 Gameplay Root；保护策略只在选定 Root Ruleset 的装配范围内生效。
4. **Importer 只规范化**：External Importer 不知道转换目标和目标 Ruleset。
5. **Mapping 只投影**：Mapping Profile 不解析原始格式，也不定义目标机制。
6. **Parser 与 Runtime 分离**：Parser 不执行算法，不创建权威实例。
7. **定义动态、运行静态**：加载期动态声明必须在 Tick 前编译为稳定 Slot、Layout 和 Plan。
8. **权威世界唯一**：缓存、索引、Snapshot 和表现状态必须可重建。
9. **稳定身份**：禁止以进程地址和临时容器位置作为存档引用。
10. **确定性优先**：Tick、事务、事件、遍历和 RNG 顺序必须显式定义。
11. **事务修改**：所有权威变化通过 Command / Transaction 原子提交。
12. **能力而非名称耦合**：跨机制依赖稳定、版本化 Capability Contract。
13. **可诊断失败**：未知字段、悬空引用、缺失映射和版本冲突不得静默忽略。
14. **数值语义明确**：权威数值必须声明精度、舍入、溢出和序列化规则。
15. **原生扩展可选**：Native Extension 只提供通用能力或性能后端，不把业务重新写回 Kernel。
16. **外部 Corpus 非规则来源**：HOI3 等 Corpus 可以提供数据和资源，但不能定义 Dillen 的最终 Gameplay Meaning。
17. **兼容声明分级**：Importer 只能声明 Source Compatibility；只有具体 Mapping Profile 才能声明对某个目标 Ruleset 的 Mapping Compatibility；Gameplay 等价性必须另行定义和验证，不能由“成功导入”自动推出。

### 1.6 Kernel 扩展判据

允许扩展 Kernel 的理由：

- 当前缺少跨 Mechanism、跨 Ruleset 可复用的 Runtime Primitive；
- 当前 Capability 无法表达一类通用、可版本化的交互；
- 当前运行时无法满足确定性、持久化、安全性或性能底线；
- 新平台后端需要统一抽象。

不允许扩展 Kernel 的理由：

- 某个 HOI3 字段难以映射；
- 某个 Mod 想绕过公开 Contract；
- 某个机制用专用 `RuntimeXXXState` 编写更快；
- 某个原版引擎存在隐藏执行路径；
- 未来也许会用到某个专用 Setter。

### 1.7 核心验收标准

Project Dillen 主体框架达到第一阶段验收必须满足：

1. 不修改或重新编译 Kernel，即可通过外部 Package 注册 Kernel 事先不知道的机制；
2. Template Source 能经过 Parser、Resolver、Registry 和 Runtime Compiler 形成 Frozen Runtime Catalog；
3. WorldBuilder 和运行期 Command 能创建稳定 Entity 与 Mechanism Instance；
4. Algorithm 能响应 Lifecycle、Tick、Event 和 Command，只通过 Query/Command/Transaction 与世界交互；
5. GUI 或测试客户端只通过 Query 与 Command 工作；
6. Entity、Component、Relation、Mechanism、RNG、Inbox、Sequence 和时间能够完整 Save/Load；
7. Schema、Definition、Algorithm 和 Ruleset 版本能够迁移或明确拒绝；
8. 同一 Package Lock、Ruleset Fingerprint、初始状态、输入和 RNG Seed 能确定性回放；
9. 删除全部 HOI3 Importer、Mapping Profile、HOI3 Compatibility Target 和 Oracle 后，Dillen 主体仍能独立构建、测试和运行；
10. 更换 Root Ruleset 后，可以装配不同 Gameplay Package，而无需修改 Kernel。

在启动 HOI3 正式移植前，还必须完成第二阶段通用能力验收：

11. 不修改 Kernel C++，能够以外部 Package 分别声明并运行科研、生产、外交、政治、情报和战区机制模板；
12. Dillen 原生内容语言具有统一、类型化、可编译的 Trigger / Effect / Scope 前端与语义层；
13. 外部静态定义文件能够经过 Parser、Resolver、Definition Registry 和 Runtime Compiler 进入冻结目录，并支持稳定引用、覆盖诊断和版本身份；
14. 上述六类参考机制只能通过通用 Entity / Component / Relation / Mechanism、Query / Command / Transaction、Event / Capability 和 Scope 语义实现，不得向 Kernel 增加领域专用类型或操作；
15. Trigger 求值无副作用且只读取同代际 Query Snapshot；Effect 只生成受验证 Transaction / Event / Capability Invocation；Scope 只负责确定上下文和目标集合；
16. 静态定义、语义程序与运行时实例均进入 Package Lock、Source Lock、Ruleset Fingerprint、Persistence 和 Migration 的既有身份边界。

HOI3 导入不用于替代这些通用能力的建设。只有第 11—16 条由纯 Dillen 参考 Package 验证后，才开始 HOI3 正式移植；移植结果只能暴露通用能力缺口，不能直接成为 Kernel 专用接口的理由。

---

## 2. Project Dillen 系统架构

### 2.1 工程架构图

```text
                     Launch Descriptor
       Root Ruleset + Extension Rulesets + Content Packages
              + Optional External Source Bindings
                              │
                              ▼
               Manifest / Dependency Resolver
              Package Lock + External Binding Plan
                              │
             ┌────────────────┴────────────────┐
             │                                 │
             ▼                                 ▼
  Dillen Native Package Sources       External Content Corpus
  Template / Static Definition /         e.g. HOI3 / TFH / Mods
  Scope / Trigger / Effect / Algorithm /
  Data / Presentation
             │                                 │
             ▼                                 ▼
  Dillen VFS + FileCatalog            Independent Corpus Importer
  + Parser Workspace                  source decode + normalization
             │                                 │
             │                                 ▼
             │                      Normalized External Source IR
             │                                 │
             │                      Mapping Profile ─────────────┐
             │                    target Contract references     │
             │                                 │                 │
             │                                 ▼                 │
             │                      Dillen Projection Artifact   │
             │                    no Runtime object creation     │
             └───────────────────────┬───────────────────────────┘
                                     ▼
                         Unified Source Workspace
                                     │
                                     ▼
                       Finalize Deterministic Source Lock
                                     │
                                     ▼
                 Resolver: Declare → Resolve → Validate
                                     │
                                     ▼
              Schema / Static Definition / Semantic Program /
              Algorithm / Spawn / Capability / Resource Registries
                                     │
                                     ▼
                   Ruleset Composition + Integrity Gate
                                     │
                                     ▼
                            Runtime Compiler
              Slot / Layout / Binding / Scope / Predicate /
                    Effect / Index / Schedule Plan
                                     │
                                     ▼
                         Frozen Runtime Catalog
              + Package Lock + Source Lock + Ruleset Fingerprint
                                     │
                                     ▼
                                WorldBuilder
                                     │
                                     ▼
┌──────────────────────────────────────────────────────────────┐
│                         Dillen Kernel                        │
│                                                              │
│                    Authoritative World                       │
│                                                              │
│ Entity / Component / Relation / Mechanism Stores             │
│ World Clock / RNG Streams / Scheduled Algorithm Inbox        │
│ Lifecycle / Algorithm Runtime / Deterministic Scheduler      │
│ Query / Command / Transaction / Event / Capability           │
│ Persistence / Migration / Diagnostics / Fault Isolation      │
└──────────────────────────────┬───────────────────────────────┘
                               │
                  Query / Command / Fact Stream
                               │
              ┌────────────────┼─────────────────┐
              ▼                ▼                 ▼
          GUI Client     Simulation AI      Tools / Debugger
                               │
                               ▼
                    Runtime Platform Services
       File System / Jobs / Renderer / Audio / Input / Window
```

架构图中的关键顺序是：

1. Importer 先独立产出 Normalized Source IR；
2. Mapping Profile 再把规范化 IR 投影到目标 Dillen Contract；
3. Native Source 与 Projection Artifact 在 Resolver 前汇合；
4. Source Lock 在 Importer、Mapping 与 Native Source Workspace 确定后才能最终生成；
5. Ruleset 完整性验证先于 Runtime Compiler；
6. Scope / Trigger / Effect 在加载期完成类型检查、引用解析和 Slot 化，Tick 期不解释可编辑字符串；
7. WorldBuilder 只消费 Frozen Runtime Catalog，不消费 HOI3 IR；
8. Kernel 对输入来源无感知。

### 2.2 关键中间产物

| 产物 | 所有者 | 内容 | 禁止事项 |
|---|---|---|---|
| Package Lock | Package Resolver | 确定的 Package 版本、依赖和顺序 | 不保存战局状态 |
| Native Package Source Lock | Source Pipeline | **每条 Source Artifact 一行**：Package Id、Package 版本、Source Layer 名、虚拟路径、内容指纹、字节长度（`kernel::SourceLockEntry`） | 不承担 Importer / Mapping 身份；不包含运行期对象地址 |
| External Projection Artifact Identity | Adapter | Corpus 快照摘要、Importer 版本与实现摘要、Normalized IR 摘要、Mapping Profile 版本与摘要，及其联合摘要（`adapter::ProjectionArtifactIdentity`） | 不进入 Native Source Lock；不直接创建 Runtime Instance |
| Ruleset Fingerprint | Runtime Compiler | 最终装配身份。**当前实现**只覆盖 Ruleset Definition + Package Lock + Native Source Lock（`ComputeRulesetFingerprint`）；**Projection Artifact Identity 尚未接入**，是 §3.18 已登记的缺口 | 不保存战局状态 |
| Parse Artifact | Parser | 语法结构、动态键、顺序和 Source Span | 不执行 Gameplay 行为 |
| Gameplay Semantic IR | Semantic Resolver | 类型化 Scope、无副作用 Trigger、事务化 Effect 及其稳定引用 | 不持有运行时实例；不直接修改 World |
| Normalized External Source IR | Importer | 与目标 Ruleset 无关的规范化外部内容 | 不引用 Dillen Gameplay Target |
| Dillen Projection Artifact | Mapping Profile | 对目标 Contract 的声明式投影结果 | 不直接创建 Runtime Instance |
| Resolved Registry Set | Resolver / Registry | 已解析和验证的 Schema、静态 Definition、Semantic Program、Algorithm、Capability 与资源 | 不允许 Tick 期修改结构 |
| Frozen Runtime Catalog | Runtime Compiler | Slot、Layout、Binding、Scope、Predicate、Effect、Index 与 Schedule Plan | 不保存当前战局状态 |
| Authoritative World | Kernel Runtime | 当前 Simulation 的唯一权威状态 | 不保存可重建表现缓存 |

### 2.3 严格依赖方向

```text
Runtime Host ──depends on──> Kernel / World / Runtime
Kernel / World / Runtime ──depends on──> Platform Abstractions

Gameplay Packages / Rulesets ──compile against──> Kernel Contracts
Native Content ──targets──> Gameplay Package Public Contracts

Mapping Profile ──depends on──> Normalized Source IR Schema
Mapping Profile ──targets──> Gameplay Package Public Contracts
External Corpus Importer ──depends on──> Generic Source / Diagnostic Foundation

HOI3 Oracle ──provides optional evidence only──> Importer Fixtures / Mapping Tests
```

约束：

- Kernel 不依赖 Gameplay Package、HOI3 Importer、Mapping Profile 或 Oracle；
- Gameplay Package 不依赖 HOI3 Importer；
- Importer 不依赖目标 Ruleset 和 Gameplay Contract；
- Mapping Profile 同时依赖 Normalized IR Schema 和目标公开 Contract，但不拥有两者；
- Runtime Host 可以组合具体 Package，但 Package 不得反向取得 Kernel 私有实现；
- Oracle 不得成为任何发布版运行依赖。

### 2.4 Ruleset 与外部来源装配

Launch Descriptor 负责选择：

- 一个 Root Ruleset；
- 零个或多个合法 Extension Ruleset；
- Content Package 集合；
- 可选 External Source Binding。

External Source Binding 只声明 Corpus、Importer 和 Mapping Profile 的版本化组合。其输出必须进入 Source Lock，并最终作为 Generated Projection Source 参与普通 Resolver、Integrity Gate 和 Runtime Compiler，不得形成旁路 WorldBuilder。

Ruleset Fingerprint 至少包含：

- Root / Extension Ruleset 身份和版本；
- Package Lock；
- Source Lock；
- Schema、Definition、Algorithm 和 Capability 版本；
- Runtime 编译策略和确定性配置。

### 2.5 Runtime 执行架构

运行时服务分为：

- **World State**：Entity、Component、Relation、Mechanism、Clock、RNG、Scheduled Inbox；
- **Execution Services**：Scheduler、Algorithm Runtime、Command Queue、Transaction Executor；
- **Read Services**：Query Snapshot、Index 和只读派生数据；
- **Output Services**：Committed Fact Stream 和 Presentation Notification；
- **Durability Services**：Persistence、Migration、Replay 和 Checksum；
- **Platform Services**：文件、线程、渲染、音频、输入和窗口。

World 只保存权威状态；Scheduler、Query Snapshot、可 Drain 事实流和表现状态不成为第二套权威世界。

---

## 3. 系统构成、当前状态与实现边界

### 3.1 Package、VFS、FileCatalog 与 Parser

**目标职责**：装配 Dillen Package，确定虚拟路径来源，读取 SourceBuffer，根据 File Dispatch Template 选择 Parser，并形成不可变 Parse Workspace。

**边界**：FileCatalog 不执行 Declare / Resolve / Validate，不创建 Runtime Instance，不解释外部 Corpus 的目标 Gameplay Meaning。

**当前实现**：`src/parser` 已具有 VFS、FileCatalog、SourceBuffer、Lexer、Parser Cursor、File Dispatch Template、Parser Registry 和基础 Parse Workspace。FileCatalog 与旧 Analyzer 的文件遍历职责已经合并；Resolver 独立承担语义 Pass。独立的 `dillen::authoring` 组件已为 Package Manifest、Capability Contract、Component Schema、Entity Definition、Relation Schema、Relation Definition、Mechanism Template、Algorithm Descriptor、Mechanism Definition、Spawn、Root Ruleset 和 Extension Ruleset 注册严格文件模板与 Parser。`AuthoringSession` 可组合多个带名称、优先级、虚拟前缀和 Replace Path 的 Package Source Layer，并将胜出的实际 Source Artifact 送入统一 Declare / Resolve / Validate 管线。

**下一缺口**：补齐结构化 List / Object / Reference 初值、Generated Source 的 Source Map、可扩展语法版本迁移和面向作者的诊断展示；这些扩展不得破坏基础 Parser 与 Kernel 的依赖方向。

### 3.2 Resolver

Resolver 固定执行：

```text
Declare stable symbols
→ Resolve cross references
→ Validate types, constraints and global consistency
→ Freeze registries
```

Resolver 不遍历文件系统、不重新读取文件、不执行算法、不创建实例。Importer 和 Mapping Profile 的结果必须作为普通 Source Artifact 进入 Resolver。

### 3.3 Manifest、Ruleset、Package Lock 与 Source Lock

**当前实现**：已建立 `PackageManifestRegistry`、`RulesetRegistry`、`PackageLockBuilder`、`SourceLock`、`RulesetFingerprint`、`RulesetIntegrityValidator` 与版本化 `RuntimeCapabilityContractRegistry/Resolver`。Package Lock 已能执行确定性版本选择、依赖闭包、拓扑排序、冲突和循环拒绝。每个 Authoring Source Layer 现在必须且只能由一个 Package Manifest 所有；Pipeline 按稳定虚拟路径对该层除 Manifest 自身以外的真实 Source 字节计算 `dillen.package.content.v1` SHA-256，并验证 `content_digest`。Source Lock 的每项同时记录 Package ID / Version、Source Layer、虚拟路径、内容 Fingerprint 和字节长度，从而把真实文件严格绑定到明确 Package 版本，而不是由任意 Package 摘要伪造。Root Source Layer 同样必须拥有被 Root 明确要求的 Package。`RootRulesetDefinition`、`ExtensionRulesetDefinition` 与 `RulesetComposer` 已取代全局不可替换 Core Ruleset 假设：启动方显式传入一个 Root，Extension 按 `priority → stable id → version` 确定性排序，仅能在 Root 允许的契约类别中执行加法组合；Root 已拥有契约、显式保留契约、Extension 重复契约、目标 Root/版本不匹配均会在组合阶段拒绝。组合结果、Package Lock 与 Source Lock 一并进入 Frozen Runtime Catalog、Ruleset Fingerprint 和持久化身份校验。

**需要修正和补齐**：

- Projection Artifact 已建立完整身份链与稳定摘要；待 External Corpus Adapter ABI 恢复后，将其生成的 Projection Lock Source 正式并入 Package / Source Lock 与 Ruleset Fingerprint；
- 在纯加法 Extension 验证稳定后，再以显式操作和逐契约授权扩展 Override Policy；未获 Root 授权的替换继续默认拒绝；
- 补齐资源要求和 Generated Source 的完整性验证。

### 3.4 Schema、Definition、Spawn 与 Algorithm Registry

**当前实现**：

- 版本化 `MechanismSchemaRegistry`、`ComponentSchemaRegistry` 和 `RelationSchemaRegistry`；
- `EntityDefinitionRegistry`、`RelationDefinitionRegistry`、`MechanismDefinitionRegistry`、`MechanismSpawnDefinitionRegistry`；
- 版本化 `AlgorithmRegistry` 和 `RuntimeCapabilityContractRegistry`；
- 递归统一值类型、字段/角色约束、默认值、引用类型和稳定排序。
- `AuthoringSession` 已按 `Package/Capability/Schema/Algorithm → Entity/Relation/Mechanism Definition → Spawn → Ruleset Composition → Integrity → Runtime Compile` 顺序执行 Declare / Resolve / Validate，并冻结全部相关 Registry；外部 Fixture 已能直接生成包含 Package Lock、Source Lock、Root/Extension 身份、Component/Relation/Mechanism Layout、Algorithm、Entity/Relation/Mechanism Definition 和 Spawn 的 Frozen Runtime Catalog。

**当前缺口**：现有 Definition 体系已经足以描述机制实例、实体、组件和关系，但还没有形成面向完整游戏内容的通用静态定义系统。后续必须补齐结构化对象与嵌套集合、枚举与符号、跨文件稳定引用、显式继承/组合语义、Definition 分类命名空间和统一 `StaticDefinitionRegistry`；科研项目、生产项目、外交动作、政治制度、情报任务与战区规则只能作为外部静态 Definition 和 Mechanism Package 内容存在，不能通过新增 Kernel 专用类型实现。

**边界**：Registry 保存加载期定义，不保存当前战局状态；Gameplay Algorithm 不得隐藏在字段校验器中。

### 3.5 Runtime Compiler 与 Frozen Runtime Catalog

**当前实现**：Runtime Compiler 已将 Mechanism / Component 字段和角色编译为 32 位 Slot，将 Mechanism / Component / Relation Schema 编译为 Type Layout，将 Entity / Relation / Mechanism / Spawn Definition 编译为紧凑向量，并冻结 Algorithm、Definition 专属 Declarative Bytecode 与 Capability Binding、Package Lock、Source Lock、Root / Extension Composition 身份和 Ruleset Fingerprint。编译入口先从组合后的 Root 要求出发，递归闭包 Definition → Schema / Algorithm、Spawn → Definition、Relation Definition → Relation / Entity、Entity → Component，以及 Declarative Program 引用的 Entity / Spawn；只有闭包内容进入 Frozen Catalog。已加载但未选择的 Registry 内容不会进入运行时布局、字节码、初始 Spawn 或 Tick 热路径。Tick 热路径不再解析 Algorithm 字段名，也不依赖顶层字段字符串 Map。

**下一缺口**：

- Index Plan 与 Schedule Plan；
- Source / Projection 诊断反射映射；
- 结构化 Object Schema 编译；
- 编译产物格式版本和可重现构建测试。

### 3.6 Entity、Component、Relation 与 Mechanism Store

**当前实现**：

- 通用 `EntityRegistry`；
- Slot 化 `ComponentStore`；
- 由 Relation Schema / Definition 约束并维护 Outgoing / Incoming 的二元 `RelationIndex`；
- `MechanismInstanceStore` 和 Definition / Type 索引；
- 稳定 Entity / Relation / Mechanism Instance ID；
- 从 Frozen Definition / Spawn 确定性创建对象。

**下一缺口**：Entity 删除、Component 动态附着/移除、角色重绑定、更多可配置引用删除策略和通用索引重建。Mechanism Destroy 已采用保守策略：存在外部 Mechanism Instance Role 引用时拒绝删除，并原子取消定向待处理事件。

### 3.7 WorldBuilder 与 Authoritative World

WorldBuilder 只消费 Frozen Runtime Catalog 和场景入口，构造临时候选 World，验证后原子发布。它不解析 HOI3 IR，不执行每日 Tick，也不保存业务专用 Graph。

**当前实现**：独立 `world::AuthoritativeWorld` 已拥有 Entity、Component、Relation、Mechanism、Algorithm Inbox、RNG Stream、Tick 和 Revision；`InitialWorldBuilder` 只依据明确 Entity Definition 与 Spawn Definition 创建初始对象。

任何 `CountryState`、`ProvinceState`、`RuntimeWarState` 等专用对象都只能属于外部适配原型或 Mapping 中间产物，不能进入通用 World。

### 3.8 Algorithm Runtime 与 Capability

**当前实现**：Algorithm Descriptor 已包含稳定 ID、版本、Backend、Create / Tick / Event / Command / Destroy 入口点、确定性声明、Execution Policy 和 Capability Requirement。Algorithm Runtime 已接通全部五个阶段；Executor 只读同代际 Snapshot、Scheduled Event 和 RNG Snapshot，只能输出 World Transaction。Declarative / Bytecode 后端支持字段设值与增量、生命周期转换、Entity / Component / Relation / Mechanism Query 数量条件、字段条件、事件类型条件、RNG 模条件，以及创建 Entity、设置 Component、增加 Relation、Spawn Mechanism、调度事件、创建和推进 RNG Stream 等通用事务指令。Runtime Compiler 在加载期解析引用并冻结为 Slot / Stable ID 字节码，内建无循环 VM 按稳定顺序执行并仅生成事务。Native 后端继续使用显式 Executor Registry。

**重要现状界定**：上述能力是可执行 Algorithm 后端和底层通用操作数/事务指令，不代表完整的 Trigger / Effect / Scope 内容语言已经完成。当前仍缺少独立的一等 Scope 选择计划、可组合且纯只读的 Trigger 语义树、只产生受控事务的 Effect 语义树，以及三者共享的 Source → AST → Semantic IR → Resolve → Validate → Frozen Program 前端。后续不得继续以向 Algorithm DSL 零散增加专用条件或指令的方式替代这层建设。

Execution Policy 提供正数确定性指令预算、受控 Script 单次切片预算、Script 权威状态内存配额、非权威墙钟警告阈值和 `isolate_instance / pause_instance / fail_instance` 三种失败策略。Declarative VM 与 Controlled Script VM 在每条字节码前消费确定性预算；Native Executor 通过 Context 中的 Tracker 协作消费预算。只有指令预算超限、Script 内存配额超限、契约错误、执行拒绝、异常或事务拒绝等确定性/语义故障才会丢弃输出、记录权威 Fault 并执行 Failure Policy。墙钟耗时只保存在当次 Invocation 诊断报告中，不中止算法、不影响事务提交、不进入 Authoritative World、Save、Replay Checksum 或生命周期。Fault State 包含隔离标记、次数、错误码、阶段和 Tick，可由显式事务清除。

Destroy 阶段在实例进入 Completed / Failed 后执行；成功输出与实例删除同事务提交，同时清理定向 Algorithm Inbox。存在其他 Mechanism Instance Role 引用时保守拒绝删除。Native C++ 回调不会被不安全地强制终止；进程级卡死保护属于非权威 Host Watchdog，且不得把墙钟结果回写为权威 Gameplay 结果。

**DSL v1 读操作数与聚合（2026-08-31 起，Demo 0.5 前置）**：在此之前 DSL 写值的操作码**全部是 `*Constant`**，运行期操作数只有编译期常量和事件载荷两种来源——没有任何指令能读一个值当操作数。用经济—科研—生产的最小领域模型反推时，三个机制**一条都写不出来**：生产要 `output = ore × efficiency`、科研要 `progress >= cost`、经济要 `Σ 各省税收`，全部不可表达。

补齐的是**一个**通用概念而不是若干专用指令：**读表达式 = 路径 + 归约**。角色槽持有引用列表，关系跳转再次展开成列表，所以"跨对象读"和"聚合"是同一件事的单值端与多值端，`reduce` 只是回答"一组值是什么意思"。

- **路径根**：`constant` / `event_payload` / `self_field` / `role_target`；
- **归约**：`require_one`（结果不唯一即 Fault，不静默取首个）/ `sum` / `count` / `min` / `max`；
- **运算**：`add sub mul div min max`，比较 `eq ne lt lte gt gte`，两侧都可以是读路径。

**数据模型强制的一条顺序**：Mechanism Instance 拥有字段和角色槽，**不拥有 Component**（Component 属于 Entity）。所以读 Component 必须先经角色槽拿到 Entity 锚点，关系跳转也必须从这样的 Entity 出发。这不是设计选择。

**Decimal 全程走定点，不走浮点**（`kernel/fixed_point.hpp`）。存储标度 10⁻²（作者可见的冻结契约），表达式内部标度 10⁻⁴（不进存档、不进 Query、可随时调整）。取 10⁻⁴ 而非更细，是因为乘法中间量带标度平方：10⁻⁶ 需要 128 位中间值（GCC/Clang 的 `__int128` 与 MSVC 的 `_mul128` 两条路径），10⁻⁴ 全程 int64。收益是**整数加法满足结合律**——聚合因此顺序无关，而浮点求和必须锁死顺序。溢出、除零、非有限值一律显式拒绝，不回绕。

**聚合按 N 计费**：一条聚合指令在程序里是一条，但消耗 N 个指令预算单位（N = 结果集大小），否则 `instruction_budget` 会被一条指令绕过。遍历顺序是二级索引顺序，已由 `kernel/sorted_id_index.hpp` 钉为升序 id。

**两条当前限制，都可后续放宽**：

1. **条件求值失败判为假，不是 Fault**。角色暂时未绑定时读不到值，该指令不触发；若判为 Fault，一个暂时为空的角色会直接隔离实例。
2. ~~**经角色读 Mechanism 字段，只能读与自身同 layout 的目标**~~ —— **已解决（2026-09-01）**。角色槽的 `reference_type` 现按 `reference_kind` 决定的哈希域接受**符号名**（此前只接受裸哈希数，实际无人可用），编译期据它解析目标 layout。**未声明 `reference_type` 现在是编译拒绝**——此前静默按**调用方自身** layout 解析 `target_field`，两个类型只要有同名字段就会读到错误槽位且毫无提示。判别力已用注入验证：覆盖夹具的 `peer` 角色指向一个 `counter` 落在不同槽位的类型，回退到旧解析后**字节数不变、校验和变**，正是“静默读错槽位”的指纹。

3. ~~**`mechanism_instance` 角色槽仍没有任何写入路径**~~——**已闭合（2026-09-01，见 C2）**。当时记录的三条封死路里，真正封死的只有一条：`.dspawn` 没有 `roles` 键。`MechanismSpawnDefinition::initialRoles` 结构、注册期 `RoleBindingsValid`、编译器下降、实例创建时的 `instance.roles = spawn->initialRoles` **全都已经在了**，只差解析器读那一个键。`.ddefinition` 只支持 Entity 是正确的（Definition 写就时实例不存在），`SetRole` 操作则是**有意不加**，理由见 C2。

上述三条都**不阻塞 Demo 0.5**：经济—科研—生产模型所需的跨对象读全部走 Component 路径。

**同期补上的三处"入口缺失"**：本轮为 DSL 反推构造覆盖 Fixture 时，连续撞到三个**下层能力齐备、DSL 入口缺失**的洞，形状完全相同：

| 缺失项 | 下层状态 | 后果 |
| --- | --- | --- |
| `remove_relation` | 指令种类、操作码、编译器下降、VM、存档冻结断言**全有** | 外部 Package 无法解除关系 |
| `.ddefinition` 的 `roles` | `MechanismDefinition::roles`、编译器 `initialRoles`、注册期 `RoleBindingsValid` **全有** | 外部 Package 声明的角色槽**必然为空**，引用侧数据模型整体不可达 |
| `.drelationdef` 的 `eol=lf` | Parser 注册了 11 个授权扩展名，`.gitattributes` 只钉了 10 个 | Windows 克隆得到 CRLF，同一 commit 两平台算出不同 `content_digest` |
| `set_component_field` 的计算形式（2026-09-01 补，见 C1） | `ComponentSetFieldCommand`、World 执行侧、读路径求值**全有**，只有 DSL 与操作码没接 | 机制能读实体 Component 但只能往回写字面量；国库可以被读成收入，**永远不会被花掉** |

四处均已补齐。**这一类洞的共同形状值得单独记住**：下层能力是完整的，缺的只是作者能写出来的那句话。
它们不会让任何测试变红，因为没有内容用得上它们——只有真去写一份完整领域模型时才会撞上。这类洞**只能靠构造覆盖撞出来**，行为测试发现不了——这正是 §4.3 要求冻结 Parse / Resolve / Compile / Diagnostic **四面**而不只是 Compile 一面的直接理由。

**`cancel_event` 仍缺，且是有意保留**：它携带运行期分配的事件序列号，源码里写死字面量只会命中"碰巧拿到那个号"的事件。加一个只能用错的语法比留着可见的缺口更糟；它等运行期读操作数进入 `schedule_event` 的返回侧后再补。

**事件取消（2026-09-01）——`cancel_events`，且不会有按序列号的形式**：`cancel_events = { type = X }` 取消本实例该类型的全部待发事件，运行期展开成 N 条既有的 `CancelEvent` 命令，**不新增 World 命令、不新增 variant 备选项、Save 仍是 v5**，按 N 计费。

**按序列号取消永远不会有 DSL 语法**，这是数据流决定的而非取舍：序列号在 `AlgorithmInbox::Schedule()` 内、即**提交期**分配，而 VM 早在此之前就已发出调度命令，无从得知。要把它写回字段就得给 `ScheduledEventScheduleCommand` 增加目标槽位——存档格式变更，且把一个**运行期内部标识**变成作者可见概念。Inbox 可读之后，"取消我挂着的某类事件"这个真实需求已被覆盖；需要更精确时可按 payload 或 dueTick 匹配，而不必让作者接触序列号。

**只能取消本实例自己的事件。**能取消别的机制的排期，就绕过了 Capability 层存在的全部契约。

**受控 Script 当前实现**：`script` Backend 已启用 Dillen 自有的确定性 Controlled Script Bytecode VM，不嵌入宿主 Lua、操作系统线程状态或不可审计的第三方 VM 堆。外部 `.dalgorithm` 可声明类型稳定的持久状态、`set/add_state`、`set/add_field`、生命周期转换、绝对跳转、条件跳转、`yield` 和 `halt`。Runtime Compiler 在加载期把状态名和字段名冻结为 Slot；VM 在指令边界按 `script_slice_instruction_budget` 抢占，将 Program Counter 与状态值作为 Mechanism Instance 权威状态，通过同一 World Transaction 原子提交。Create / Tick / Destroy 在对应阶段继续执行；被抢占的 Event / Command 帧在后续 Tick 优先恢复，当前最小语言不读取未持久化的宿主事件对象。

Script 状态使用确定性的结构化字节占用模型执行 `script_memory_limit_bytes` 配额；超额会丢弃整次输出并产生 `ScriptMemoryQuotaExceeded` 权威 Fault。状态值、每阶段 Continuation、Fault 与所有相关 Sequence 已进入 Save Format v4、候选世界验证和 Replay；v3 Codec 读取继续保留，以便显式 Runtime Migration。`controlled_script_probe` 已覆盖外部语法解析、加载时编译、自动抢占、跨 Tick 恢复、Save / Load 后续执行一致性与内存配额拒绝。

**Capability 调用 ABI —— 当前是"Capability 级 fire-and-forget 闭环"，不是完整通用 RPC 式 ABI**。当前能力：单向、单 Operation（`RuntimeCapabilityContract::operations` 已定义并在注册期校验，但尚未进入调用指令、命令或投递路由）、无返回值、无关联 ID。多 Operation / 返回值 / 关联 ID 是后续设计（见本节末"下一缺口"）。以下描述的是这个 fire-and-forget 闭环。

Mechanism Definition 用 `provides_capabilities` 声明其实例响应的契约（裸名 = 接受 Ruleset 锁定的任意版本，或 `requirement = { name minimum_version maximum_version }` 声明范围）；Declarative Algorithm 用 `invoke_capability = { capability delay priority payload [target_role] [version] }` 发起一次调用，消费者只写契约名，不引用任何提供者的 Entity / Mechanism / Instance ID。

- **版本协商（9b）**：`invoke_capability` 与 `provides_capabilities` 的版本区间在加载期解析为**一个具体版本**。`ResolveCapabilityVersion` 只在 **Package Lock 声明为某锁定包所提供**的契约版本里区间内取最高（不再扫描整个 Contract Registry）——所以 Kernel 编译自封闭：只能绑定锁定包集真正拥有的版本。请求/声明区间与锁定包提供的版本都不相交 → 编译诊断 `AlgorithmProgramOperandInvalid`。字节码携带解析出的 `capabilityVersion`。（提供契约的包 manifest 用 `provides = { capability = { name version } }` 声明；一个 manifest 每个契约只能声明一个版本。）
- **定向单提供者（9a）**：`target_role` 命名调用方机制自身的一个角色 Slot；Runtime Compiler 把它解析为 `MechanismRoleSlotId`，Declarative VM 运行期从该 Slot 读出目标实例 ID 写入 `InvokeCapabilityCommand.targetInstance`（Slot 未绑定 mechanism 实例 → Fault）。省略 `target_role` = 广播。
- **投递**：VM 只发一条 `InvokeCapabilityCommand`；World Transaction Executor 按稳定 Instance ID 顺序扇出——广播时对每个 `provides_capabilities` 命中且**版本相符**的实例投递（版本不符者跳过，与零提供者一样无害）；定向时只投递给 `targetInstance`，且该实例必须存在并提供相符版本，否则事务 `Failure`（定向未命中是作者错误）。投递事件类型由契约名派生（独立哈希域）。
- **Save v5**：`InvokeCapabilityCommand`（命令 tag 10）新增 `targetInstance` 与 `capabilityVersion` 两字段，`kCurrentRuntimeSaveFormatVersion` 4 → 5；无线上 v4 存档含该命令，loader 对非当前版本按既有身份拒绝策略处理，无需 4→5 迁移器。投递复用既有 Scheduled Event 的 `type` 字段，Scheduled Event 存档格式本身不变。
- `capability_invoked = <契约名>` + `from_payload = yes` 供提供者在 `event` 阶段识别调用并读取消费者数值。`capability_invocation_probe` 覆盖 8 个用例：开放协商→锁定版本；显式 v2 命中投递；消费者 pin v1 而包只拥有 v2 → 编译拒绝；pin 不存在的 v3 → 拒绝；提供者版本下限高于所拥有版本 → 拒绝；零提供者无害；Controlled Script pump 的 `invoke_capability` 经 script 阶段进入编译闭包并投递；两个包提供同一契约 → 编译拒绝；**`target_role` 端到端**（两个 sink 提供同一契约，pump 的 `preferred` 角色绑定其一 → 只有被点名的实例 +payload，另一个保持 0；去掉 `target_role` 该用例即失败，已验证判别力）；**Save v5 往返**（排队的 `InvokeCapabilityCommand` 带 `targetInstance` + `capabilityVersion`，Save→Load→Save 逐字节一致且字段保留）。

**9c —— Controlled Script 与 Declarative 等价（运行期 + 加载期闭包）**：新增 `ControlledScriptInstructionKind::Transact`，内嵌一条 `AlgorithmInstructionDefinition`（附 `when` 条件）。

- **运行期共享**：抽出 `runtime/bytecode_transaction.{hpp,cpp}` —— `EvaluateBytecodeConditions` + `EmitBytecodeTransaction`（全部 13 个字节码 opcode → World 命令，含字段读改写、`from_payload`、`target_role` 角色 Slot 读取、生命周期门）。Declarative VM 与 Controlled Script VM 都调它 —— **两个后端的运行期行为不可能漂移**，只可能在"先报哪条编译诊断"上无害地不同。
- **编译期下降**：`runtime_compiler.cpp` 的 Controlled Script 分支里一个 `[&]` 捕获全部的 `lowerTransact` lambda，把 `Transact.action` 降为 `AlgorithmBytecodeInstruction`（与 Declarative 下降逻辑镜像；未做物理抽取以避开冻结前对编译器最密的 ~350 行做搬运的风险）。抽出共享 `IsValidAlgorithmInstruction`（`algorithm_program.hpp`）供两个校验器复用。
- **编译期依赖闭包（审核修复）**：`BuildCompileSelection` 原来只遍历 `algorithm->program.stages`；已抽出 `closeOverInstruction` lambda，同时遍历 `algorithm->script.stages` 的 `Transact.action`——Script 独占引用的 Capability / Entity / Spawn 现在与 Declarative 一样进入 Frozen Catalog。`capability_invocation_probe` 的 Controlled Script pump 用例守住此回归。
- **Parser**：`script` 阶段里 `set_field` / `add_field` 带 `when` 或 `from_payload` → 走 `ParseAlgorithmFieldInstruction` 包成 `Transact`；`create_entity` / `set_component_field` / `add_relation` / `remove_relation` / `spawn_mechanism` / `schedule_event` / `cancel_event` / `create_rng` / `advance_rng` / `invoke_capability` → 走 `ParseGenericAlgorithmInstruction` 包成 `Transact`。裸 `set_state` / `add_state` / `set_field` / `add_field` / `transition_lifecycle` / `jump` / `jump_if_state_equals` / `yield` / `halt` 仍走原生路径（零风险）。
- **无 Save 格式变化**：Controlled Script 持久化的是 state 值 + Continuation PC，不是字节码。
- `controlled_script_probe` 扩展：DSL 里 `add_field when=` 与 `schedule_event` 解析为 `Transact`；一条守卫 `Transact`（`field_equals counter==0` 时 `add_field +5`）经共享路径执行，Tick 1 +5、Tick 2 守卫失败不变。

**契约归属（第二轮审核修复）**：一个 Capability Contract 身份在一次组合 Ruleset 里**只能由一个 Package 提供**（`RejectAmbiguousCapabilityProviders`，编译期 `IntegrityValidationFailed`）。原因是 Kernel 里根本无法判定归属：`MechanismDefinitionSource` 只有 Source Layer 名，`AlgorithmDescriptor` 连来源字段都没有。若两个包分别提供 v1/v2，`ResolveCapabilityVersion` 的"区间内取最高"会让某个 Definition 绑定到**另一个包**声明的版本。拒绝歧义比补一条完整归属链更小且更可审；冻结前收紧、将来要放宽（真正的多提供者模型）随时可以，反向则不行。

**下一缺口（尚未闭合）**：

- **多 Operation / 返回值 / 关联 ID 的通用 Capability ABI（= Capability ABI v2）**：当前是单向 fire-and-forget。`operations` 字段已在契约里（`runtime_capability_contract.hpp`），但只被注册期校验，调用指令、World 命令与投递路由都还没用它。要成为可被多种实现自由替换的通用 ABI，需要：调用点指定 Operation、投递携带关联 ID、提供者可回一条关联响应。这是 Capability 层最大的一块未做工作。
  **与 Demo 0.2 冻结的关系**：v2 **不阻塞**冻结；Demo 0.2 冻结的是 fire-and-forget v1（见 §4.2「冻结范围界定」）。v2 必须以**纯加法**方式引入 —— 新指令或新命令 tag + Save 版本升级 —— 不得改变 v1 的语义或既有存档布局。
- **正式的 Definition / Algorithm → Package 归属链**：上面的"单一提供者"约束是用拒绝歧义换来的安全，不是真正的归属判定。若将来要支持一个契约的多版本并存，必须先给 `MechanismDefinitionSource` 与 `AlgorithmDescriptor` 补 `PackageId`，并按依赖闭包（而非全部锁定包）解析版本。

### 3.9 Scheduler、Transaction、Event、Query 与 RNG

**当前实现**：

- World Transaction 已统一暂存 Entity、Component、Relation、Mechanism、Scheduled Event 和 RNG Store；
- 任一命令失败时不留下跨 Store 半更新；
- Command Queue 按 `notBeforeTick → priority → sequence` 稳定排序；
- Algorithm Inbox 按 `dueTick → priority → sequence` 稳定排序；
- Committed Fact Stream 与权威 Scheduled Inbox 已分离；
- **Scheduled Event Inbox 已成为 Query 快照面（2026-09-01 新增，只读）**：`WorldQuerySnapshot::ScheduledEvents()` 按值持有写时复制的 `AlgorithmInbox`，发布只是引用计数递增。此前算法能调度事件却**看不见任何待发事件**——`AlgorithmInvocationContext` 只带 Query、Mechanism 视图和 RNG。`Pending()` 已按 `dueTick → priority → sequence` 有序，枚举天然确定，无需额外排序。
- **事务审计事件不再回灌算法事件队列（2026-08-31 语义变更，需显式记录）**：`ApplyImmediateCore` 现携带 `dispatchResultToAlgorithms`，只有**外部/宿主提交**的事务（`ApplyImmediate`）其结果事件会进入 `pendingAlgorithmEvents_` 并在 Event 相位派发；**算法自身产生的事务**（`ApplyAlgorithmReport` / `ApplyAlgorithmFault` / 批量提交路径）其审计事件仍进入 Fact Stream，但不再回灌给算法。
  **改这个的理由**：Fact Stream 是审计与 Replay 的输入，不是 Gameplay 事件通道。回灌会让每个算法事务在下一相位再次广播给全部合格实例，形成与玩法无关的自激放大——`thread_contract_probe` 同一夹具的 fact 数由此从 **21661 降到 105**，`scale_probe` 的 Release 耗时在 N≥4000 时下降 13–16%。
  **这是对外可观察的语义变更**，不是纯优化：此前算法能在 Event 相位看到其他算法事务的审计事件，现在看不到。现有黄金值全部未动（存档格式、Replay Checksum、Demo 内容均不依赖该回灌），但外部 Package 若曾依赖它，行为会变。**需要跨算法通知的场景应使用 Capability 调用或 `schedule_event`，那才是有契约的通道。**
- RNG Stream 使用稳定 ID、Seed、Draw Count 和 Expected Draw Count；
- `WorldQuerySnapshot` 在单次发布中深拷贝 Entity、Component、Relation 与 Mechanism 四个权威 Store，并统一绑定同一 `Publication / Tick / Revision`；
- Entity 查询支持 Stable ID、Definition 和 Type 索引；Component 查询支持 Owner / Type、字段 Slot、按类型查 Owner 和按 Entity 查 Component Type；Relation 查询支持 Stable ID、Type、Outgoing 和 Incoming 索引；Mechanism 查询支持 Stable ID、Definition、Type、字段 Slot 和角色 Slot；
- `WorldQueryService` 通过不可变 `shared_ptr<const WorldQuerySnapshot>` 发布快照；调用方可跨后续 Tick 持有旧代际，旧快照不会被原地覆写；
- `KernelRuntime::AcquireQuerySnapshot()` 是 GUI、AI、工具和未来 Standalone Host 的稳定查询入口，`KernelRuntime::Query()` 用于当前调用栈内即时读取；原 `Snapshot()` 暂保留为 Mechanism 子视图兼容入口；
- Algorithm Runtime 已接收完整一致 Query Snapshot；算法可读取四类通用世界对象，但仍只能通过 Command / Transaction 修改权威世界；
- RNG Snapshot 与 World Query Snapshot 在每次 Runtime 发布时使用相同 Tick / Revision；
- Scheduler 的物理归属已从 Kernel 契约层迁入 `src/runtime`，由 `KernelRuntime` 持有并编排 Tick；Kernel 只保留可复用的状态、事务和编译契约；
- 纯 Dillen（关闭 HOI3 Compatibility 与 Oracle）Windows x64 测试清单在 **Demo 0.5 封存时为 33 项**，Linux gcc / clang CI 也配置为运行同一清单。**那一刻的 Debug 与 Release 均为 33/33；截至 2026-09-03，启用 Presentation 与 GL 地图后端的 `dillen-map-renderer-windows-x64` 配置已在 Debug / Release 均通过 44/44，现状以 §4.4 结尾的「当前基础状态」为准**；此前 `runtime_catalog_probe` 新增必填角色拒绝用例遗漏 Ruleset `requiredDefinitions` 的接线已经在 Demo 0.5 封存提交中补齐。`demo_0_5_vertical_slice_probe`、四面 Authoring 黄金值和 Kernel 工程验证夹具均通过。`thread_contract_probe` 守住派发的顺序无关性；`dillen_demo_1_0_probe` 覆盖双外部机制包、真实 Package / Source Lock、可替换 Root、Query、Scheduled Event、RNG、Capability 跨机制调用和权威事务结果，`controlled_script_probe` 与 `projection_adapter_probe` 分别覆盖受控脚本和 Adapter 身份迁移，`mechanism_ids_probe` 冻结 Stable Identity 层的哈希输出，`capability_invocation_probe` 固化契约调用的解耦闭环，`scale_probe` 是代表性规模的正确性与耗时基线，`demo_0_5_vertical_slice_probe` 守住正式玩法闭环与 Package 门禁，`architecture_guard_probe` 把模块分层依赖与“无 HOI3/oracle include”变成源码级门禁。当前未在本机重跑 Linux 组合，Linux 状态仍以阻塞 CI 为准。启用冻结 HOI3 Compatibility 后的旧兼容夹具仍引用整理前的仓库 Corpus 路径；按照当前冻结策略，它们将在未来 Adapter 恢复时改为测试显式传入的实际 Corpus Root，不在 Standalone 主线中临时回接旧路径。

**线程契约（与 Snapshot / Transaction 契约同批冻结）**：

**Kernel 可以在 Tick 相位内使用工作线程执行算法派发。这不改变任何权威结果。**允许并行的只有算法派发——即"读不可变快照 → 产出 `WorldTransaction`"这一段纯函数计算。

**但"可并行"按后端分级，不是全体适用**（2026-08-31 修订，此前表述过于乐观）：

| 后端 | 并行资格 | 依据 |
| --- | --- | --- |
| Declarative Bytecode | **默认可并行** | 由内建无循环 VM 执行，除不可变快照与冻结 Catalog 外无任何输入 |
| Controlled Script | **默认可并行** | 同一 VM 与同一下降形态；Continuation 是每次调用的局部值，提交期才写回 |
| Native C++ Executor | **默认串行** | `AlgorithmExecutor` 是 `std::function<bool(const AlgorithmInvocationContext&, AlgorithmExecutionOutput&)>`（`runtime/algorithm_runtime.hpp`）——**任意闭包**。`const std::function` 只约束调用者不改函数对象本身，对闭包捕获的共享可变状态没有任何约束 |

原先的论证是"`AlgorithmRuntime` 只持有 `catalog_` / `executors_` 两个 const 引用，无可变状态，`Invoke` 是 const"。这对前两种后端成立，**对 Native 后端不成立**：Runtime 自身无状态，不代表它调用的闭包无状态。一个捕获了 `shared_ptr<Cache>` 或静态计数器的 Native Executor，在 Worker Pool 下就是数据竞争与非确定性。

未来放开 Native 并行必须走**显式契约**：在 `AlgorithmExecutorBinding` 上增加 `parallel_safe` 声明，由注册方承诺、并由探针验证；**未声明即串行**。不得靠"实现看起来没状态"来推定。

以下**永远单线程**，按稳定 Instance ID 顺序：

- 全部 `WorldTransaction` 的应用与提交；
- Stable ID / Sequence / 创建序号的分配；
- 事件发布、快照发布、`revision` / `tick` 更新；
- RNG Stream 的推进。

**工作线程被禁止**：触碰 `AuthoritativeWorld`（它在派发路径上不可达）、分配任何稳定标识、发布任何事件、让异常越过线程边界。

**确定性由构造保证，不由调度保证**：派发结果写入按枚举位置预先定长的槽位，顺序只取决于快照枚举顺序，与完成顺序无关。**线程数对权威输出无影响**——1 线程与 N 线程必须产生逐字节相同的存档与 Replay Checksum，这是验收项而非期望。

墙钟时间在并行下更不可信，继续只作为非权威 Invocation 诊断，不得进入权威状态、存档或 Failure Policy（沿用既有规则）。

**契约当前成立的依据（已核实，范围限于 Declarative / Controlled Script 后端）**：`AlgorithmRuntime` 只持有 `catalog_` / `executors_` 两个 const 引用，无可变状态；`Invoke` 声明为 `const`；`AlgorithmExecutionBudget` 每次调用局部构造（`algorithm_runtime.cpp`）；异常已在 `Invoke` 内部捕获并转为 Fault；派发本来就是"全部实例先对同一份快照跑完、再统一提交"，并行不引入新语义。RNG 特别说明：算法从不可变 `DeterministicRngSnapshot` 读取，`advance_rng` 携带从快照读到的 `expectedDrawCount`；同相位内两个实例推进同一 Stream 时，第二条在提交期因 `expectedDrawCount` 不符被拒——这是今天已有的行为，并行不改变它。

**结构已落地（2026-08-31）**：派发改为两相位——相位一按快照枚举序建立调用计划并决定全部槽位，相位二把 `plan[i]` 的结果写入 `invocations[i]`。此前六个 Dispatch 都在过滤循环里 `push_back`，执行顺序与槽位顺序是焊死的，**契约描述的结构当时并不存在**。

**守卫现状**：

| 断言 | 状态 |
| --- | --- |
| 填槽顺序不影响任何权威字节 | **已守卫** —— `thread_contract_probe` 用 `DispatchExecutionOrder::Reversed` 把整个世界跑两遍，存档镜像与 Fact Stream 必须逐字节相同（两条断言均以注入-回滚验证过判别力） |
| 并发执行下的内存安全 | **无守卫** —— 逆序只证明顺序无关，不证明线程安全。1-vs-N 对拍探针必须与 Worker Pool 同批落地 |
| Native Executor 的并行安全 | **无守卫，且探针不覆盖** —— `thread_contract_probe` 用的是 Declarative 后端、空 Executor Registry，完全没有触及 Native 路径 |

**实现形态与顺序**：线程池放在 `src/runtime`，**不进 Kernel**（Kernel 是业务无关契约层，不该知道线程）；Host 可配置线程数且**必须能设为 1**；低于实例数阈值仍走单线程。实测派发仅占 Tick 的 ≈21%（N=4000：派发 32 ms / 提交 79 ms），按 Amdahl 定律完美并行也只能快 **1.27×**，因此**契约随冻结定下，实现排在粗粒度 CoW 之后**（CoW 针对的是占 70%+ 的提交侧）。

**下一缺口**：

- 脏索引、结构共享和增量快照优化；当前实现优先保证不可变性与跨 Store 一致性；
- 阶段级预算、明确 Scheduler Phase Contract；
- 更细粒度事务暂存（粗粒度 CoW），替换当前每个 Tick 相位一次的全 Store Copy —— 逐**事务**的全 Store Copy 已由 `WorldTransactionBatch` 消除，见 §3.20；
- Scheduled Event 消费审计和持久化屏障。

### 3.10 Persistence、Migration 与 Replay

**必须保存**：

- 世界格式、Package Lock、Source Lock、Ruleset Fingerprint；
- Entity、Component、Relation、Mechanism Instance 和算法权威状态；
- World Clock、RNG Stream、Sequence、Command Queue、Scheduled Inbox；
- 无法由稳定 ID 重建的权威引用。

**不得保存为权威状态**：

- Query Snapshot、派生索引、缓存、表现状态和进程地址；
- 可由事务重建的 Committed Fact Stream；
- Importer 临时 Parser 对象和未锁定 SourceBuffer。

**当前实现**：`Project-Dillen/src/persistence` 已成为独立 Durability 组件，并完成以下最小闭环：

- `RuntimeSaveImage` 当前格式版本为 5（v4 加入 Controlled Script 状态/Continuation；v5 给 `InvokeCapabilityCommand` 加 `targetInstance` 与 `capabilityVersion`），保存 Active Ruleset、Extension 列表、Ruleset Fingerprint、完整 Package Lock，以及逐个记录 Package ID / Version、Source Layer、虚拟路径、内容 Fingerprint 和字节长度的真实 Source Lock；
- 保存 Entity、Component、Relation、Mechanism Instance、Mechanism Algorithm State / Fault State、World Tick / Revision、Mechanism 创建序号、Scheduled Inbox、RNG Seed / Draw Count、待执行 Command Queue，以及 Command / Inbox / Fact 的下一稳定 Sequence；
- Query Snapshot、反向索引、当前 Fact Queue、Algorithm 阶段报告和表现状态不进入 Save Image；加载成功后由 Store 和 Runtime 重新构造；
- `RuntimeSaveCodec` 使用受限递归、长度上限、确定性字段顺序、固定小端编码和整包校验值生成 Canonical Binary Save；相同权威状态产生相同存档字节；
- `RuntimePersistenceService` 先在候选 World 中验证 Stable ID、Catalog Layout、字段值、角色基数、权威引用和各类 Sequence，再一次性替换 World / Queue；任何失败都不会部分污染现有世界；
- `RuntimeMigrationRegistry` 以“Save Format + 源 Ruleset Fingerprint”为唯一迁移入口，只允许冻结后的显式单路径迁移；每一步在 Save Image 副本上执行，成功后切换至声明的完整目标 Identity，禁止静默兼容；
- `DeterministicReplayService` 从 Save Image 恢复独立 Runtime，按 `submitTick` 和日志顺序重放 Command，在每 Tick 收集 Canonical Fact Stream，并输出终态存档、Fact Stream 及两类稳定 Checksum；
- `persistence_replay_probe` 已覆盖全部四类 Store、Package / Source Lock、Clock、RNG、Inbox、Queue、创建序号与三类 Sequence 的存档往返，验证损坏存档拒绝、不兼容 Ruleset 拒绝、旧格式 Schema Migration、派生索引重建，以及双次 Replay 的 Fact Stream 和最终状态逐字节一致。
- `dillen_demo_1_0_probe` 进一步使用聚落增长与贸易周期两个真实外部 Gameplay Package 验证三 Tick 存档恢复、双次确定性 Replay、Source Lock 篡改拒绝、Package 源文件摘要篡改拒绝和跨 Root 读档拒绝。

**边界**：Durability Core 只返回内存字节，不直接承担文件路径、云存储或平台对话框；Standalone Host 已在 Platform 边界实现受限文件读取与同目录临时文件原子替换，并继续通过 `RuntimePersistenceService` 完成身份、格式与候选世界验证。Controlled Script 不持有独立宿主 VM 堆；其状态值和 Continuation 已作为普通权威状态进入统一 Codec、Migration 与 Replay 契约。

### 3.11 GUI、AI 与平台宿主

GUI、AI 和工具必须只通过 Query、Command 与 Fact Stream 使用世界。

现有 Script GUI 是 `hoi3oracle` 中已经实机验证的注入式表现系统，可以作为声明式 GUI 模型和交互协议参考；D3D9 Hook、HOI3 Lua Bridge 与进程内宿主不是 Dillen Standalone 的发布后端。

**当前实现**：`src/host` 已形成独立 `dillen::host` Platform 组件和 `project-dillen` CLI 可执行文件。`StandaloneSession` 复用 AuthoringSession、FileCatalog、Resolver、Frozen Runtime Catalog 与 InitialWorldBuilder，从一个或多个具名 Package Source Layer、显式 Root Ruleset和 Extension Ruleset 启动纯 Dillen 世界；每层可配置优先级、虚拟前缀、Include Pattern 和 Replace Path，Host 不复制 Parser、不注入机制业务语义。CLI 通过可重复的 `--source <name>@<priority>=<path>` 参数装配多层内容，同时保留单内容根兼容入口。`CliInspector` 提供 `status`、`list`、`show`、`tick`、即时 `set`、排队 `enqueue`、`save`、`load` 和 `quit`，状态读取只使用一致 Query Snapshot，修改只生成统一 World Transaction；字段输入按 Frozen Schema 类型解析，非法命令与非法值被隔离并诊断。CLI 既支持交互终端，也支持命令文件，适合作为 Demo、自动化和未来窗口宿主的最小控制平面。

纯 Dillen 主体不要求复刻完整 HOI3 GUI。确定性 AI 属于 Simulation Algorithm Client；表现 AI 或外部辅助工具只能提交受验证 Command。窗口后端必须复用 `StandaloneSession` 和 Query / Command 协议，不得绕过 Host 重新持有第二套世界状态。

**当前已经落地的 Presentation / Map 纵向管线**：

1. `Presentation Source → Parser / Resolver → Presentation Schema / Registry → Presentation Compiler → Frozen Presentation Catalog`，运行期不再解释可编辑字符串布局；
2. Asset Registry 统一管理贴图、字体、本地化和后续音频资源，并以稳定资源 ID 供控件引用；
3. 通用窗口、文本、图片、按钮、列表、进度条、Tooltip 与 Map Widget 使用同一控件树、层级、布局、输入和事件模型；
4. 数据绑定只读取同代际 `WorldQuerySnapshot`，动作绑定只产生经过 Contract 校验的 Command / Capability Invocation；
5. Map Widget 的拾取结果必须是稳定 Entity ID。地图区域本身不成为 Kernel 专用 `Province` 类型：权威地理对象由普通 Entity 表达，邻接由 Relation 表达，归属、资源、人口等由 Component 表达；
6. 权威地图拓扑和初始数据属于 Content / Ruleset 身份，几何、底图、颜色、图标、相机和高光属于 Presentation 身份；
7. Standalone Renderer、Window 与 Input 只属于 Platform / Presentation Backend，不进入 Authoritative World、Save 或 Replay Checksum；
8. 关闭或替换 GUI 后，世界继续运行；重新打开、读档或切换表现包时，GUI 必须从 Query Snapshot 重建显示，不保存第二套 Gameplay 状态。

`Dillen-Game` 可以定义“地区”“政治实体”“经济”“科研”“生产”等官方参考内容，但这些名字只能存在于 Contract / Mechanism / Content / Presentation Package，不得进入 Kernel、World、Runtime 或通用 Host 的业务分支。地图与 Presentation 的现有实现作为基础设施和回归样本保留，不再决定后续产品 Demo 编号。

### 3.12 HOI3 Importer

**当前原型位置**：`src/parser/parsers/hoi3`、`src/compatibility/hoi3/content` 和部分 `src/compatibility/hoi3/worldbuilder`。

**当前状态**：已覆盖 Country Tag、Country Definition、Province、Region、Country / Province History、UnitType、Technology、UnitModel、OOB、Diplomacy History、War History 和 Scenario / Bookmark 的首批解析切片。

**架构处置**：

- 从现在起冻结横向功能建设；
- 不再把解析结果直接转换为 Dillen Runtime Representation；
- 未来迁移为独立 `adapters/hoi3/importer` Target；
- 输出只允许是版本化 Normalized HOI3 Source IR 与诊断；
- Dillen Standalone 默认构建不得依赖该 Target。

### 3.13 Mapping Profile

**当前状态**：尚未建立正式 Mapping Profile Schema、Registry、Compiler 和 Source Lock 集成。

**未来组成**：

- Mapping Profile Manifest；
- Source IR Schema Requirement；
- Target Gameplay Contract Requirement；
- 字段、枚举、单位、引用、资源和历史 Patch 映射；
- Unsupported / Ignore / Default / Error 策略；
- Projection Artifact 与 Source Map；
- Mapping Profile 版本、摘要和兼容范围。

Kernel 工程验证夹具（目录与 Probe 仍沿用历史名称 `dillen_demo_1_0`）已经通过；该名称只是一项历史命名遗留，不再对应未来产品 Demo 计划。通用 `dillen::adapter` 基础层现已建立 Projection Artifact Identity 与 Adapter Migration：身份同时锁定 Corpus Snapshot、Importer 实现、Normalized IR Schema / Digest、Mapping Profile、目标 Root Ruleset 和生成 Source / Source Map 摘要；产物篡改会被拒绝，并可生成作为普通 Generated Source 进入 Package 的 Projection Lock Document。Migration Registry 只允许冻结后的显式身份迁移，要求 Corpus Snapshot 不变、每步输出重新封印并验证；无路径、歧义路径、转换拒绝和非法输出均独立诊断。该层不解析任何 HOI3 语义，也不绕过 Resolver 创建 Runtime 对象。

真实 External Corpus Importer / Mapping Profile 在通用 Gameplay Authoring、Trigger / Effect / Scope 和静态定义能力完成前保持冻结；恢复时必须把 Projection Lock Document 作为普通 Source 纳入 Package / Source Lock，禁止把 Adapter 身份藏入 Kernel 或 Tick 热路径。

### 3.14 HOI3 Oracle

`hoi3oracle` 保持独立 Target，继续保存 Module / Hook / Lifecycle、Native Query / Effect、SaveLoaded Barrier、Reverse Probe Framework、Script GUI 和启动器。

Oracle 原则上冻结横向逆向，仅在未来 Importer 的来源规范化或 Mapping Profile 的可观察语义存在明确歧义时增加最小 Probe。Probe 结果进入 Fixture、IR 规范或 Mapping 证据，不成为 Kernel 隐藏依赖。

### 3.15 Diagnostics 与 Fault Isolation

加载期至少诊断：

- Package / Source / Mapping 版本冲突；
- 未知字段、损坏编码、无法规范化的 Corpus 节点；
- 未映射 Source Semantic；
- Target Contract 缺失；
- 悬空引用、Schema 不兼容、Algorithm / Capability 缺失；
- Runtime Freeze 不可重现。

运行期至少诊断：

- 非法 Command 和生命周期转换；
- 事务冲突和回滚；
- Algorithm 超预算、异常和 Capability 调用失败；
- 存档版本和迁移失败；
- 确定性校验差异。

### 3.16 测试分层

1. Parser / Importer Source Fixture 测试；
2. Mapping Profile Projection 测试；
3. Manifest、Ruleset、Package Lock、Source Lock 和 Fingerprint 测试；
4. Resolver、Schema、Definition、Runtime Compiler 与 Freeze 测试；
5. WorldBuilder、引用完整性和 Store 测试；
6. Lifecycle、Algorithm、Transaction、Event、RNG 和 Query 测试；
7. Persistence、Migration 和 Replay 测试；
8. Standalone Host、GUI Contract 与端到端能力验收；
9. Oracle Probe 回归测试。

Importer 测试只证明规范化；Mapping 测试只证明投影；Gameplay 测试由目标 Mechanism Package 和 Ruleset 负责。三类测试不得混写成一个“HOI3 兼容成功”结论。

### 3.17 当前目录与目标边界

| 目录 | 当前职责 | 长期定位 |
|---|---|---|
| `Project-Dillen/src/kernel` | ID、Schema、Registry、Compiler、Capability、事务和运行原语 | 业务无关 Kernel 契约 |
| `Project-Dillen/src/world` | Authoritative World、Store、WorldBuilder、跨 Store 事务 | 唯一权威状态层 |
| `Project-Dillen/src/runtime` | Scheduler、Algorithm Runtime、Queue、Snapshot 和执行编排 | Kernel Runtime 服务 |
| `Project-Dillen/src/persistence` | Save Image、Canonical Codec、Migration Registry、Replay 和 Checksum | 独立 Durability 服务，不保存派生 Snapshot |
| `Project-Dillen/src/host` | 纯 Dillen Session 启动、CLI Inspector、Command 输入、状态展示与原子存档文件 I/O | Standalone Platform Host；只依赖公开 Authoring / Query / Command / Persistence 契约 |
| `Project-Dillen/src/parser` | 通用 VFS、FileCatalog、Lexer、Parser Registry、Resolver | Dillen Native Source 前端基础设施 |
| `Project-Dillen/src/adapter` | Projection Artifact 联合身份、内容封印、Source Map 验证、Projection Lock 与 Adapter Migration Registry | 加载期 External Corpus 边界；不含 Gameplay Semantic，不进入 Tick 热路径 |
| `Project-Dillen/src/parser/parsers/hoi3` | 当前 HOI3 Parser 原型 | 冻结并迁往独立 Importer Target |
| `Project-Dillen/src/compatibility/hoi3` | 当前 HOI3 IR 与转换期原型 | 冻结；拆分为 Importer IR，删除 Runtime WorldBuilder 职责 |
| `Project-Dillen/hoi3oracle` | 注入、Hook、原生访问、Probe、Script GUI | 独立研究平台，不是 Standalone 依赖 |
| `Project-Dillen/tests` | 当前 Dillen 与部分 HOI3 原型测试 | 逐步拆分 standalone / adapter / oracle 测试边界 |
| `Dillen-Game` | 正式 Dillen Authoring Source；Demo 0.5 的唯一内容真相源 | Contract / Mechanism / Content Package 与后续游戏内容树 |
| 计划中的 `adapters/hoi3/importer` | 无 | 独立 HOI3 Source Normalizer |
| 计划中的 `mappings/hoi3/*` | 无 | 独立 Mapping Profile 与 Projection 测试 |

**`Dillen-Game` 的当前状态（2026-09-02）——已经进入正式管线并完成领域优先重组。** `demo_0_5/contracts`、`economy/demo_0_5`、`technology/demo_0_5`、`production/demo_0_5` 与 `demo_0_5/content` 均拥有显式角色 Manifest 和真实 SHA-256 `content_digest`，共形成 5 个锁定 Package、29 个 Source Lock 条目。地图纵向切片分别落在 `map/contracts`、`map/world`、`production/map_world` 与 `presentation/map_world` 四个独立 Source Layer。原 `Project-Dillen/tests/fixtures/dillen_demo_0_5` 最小夹具已删除，前端黄金值和综合验收探针均直接读取 `Dillen-Game/`，因此不会再出现测试夹具与正式内容分叉。

`Dillen-Game` 采用**领域优先、角色隔离**的物理结构：一级目录表示内容领域；同一领域内的 Contract、Mechanism、Content、Presentation 仍是分别装载且一层一 Manifest 的 Source Layer。目录位置不产生隐式加载语义，只有 `.dpackage`、显式 Source 配置与 Root Ruleset 决定装配关系。不得为了目录直观而把多个角色塞进同一 Source Layer。

`Dillen-Game/common` 中的 `.txt` 与无扩展名文件仍是未来游戏内容草稿，不属于 Demo 0.5 的五个 Source Layer，也不会进入其 Package Digest、Source Lock 或 Frozen Catalog。`common` 不是隐式全局命名空间；其中内容日后正式接入时仍必须拥有明确 Package、被依赖方显式声明依赖并固定授权格式。无扩展名文件若参与摘要，还需显式钉死跨平台行尾。

顶层 CMake 默认关闭 `DILLEN_BUILD_HOI3_COMPATIBILITY` 与 `DILLEN_BUILD_HOI3_ORACLE`。`dillen-standalone-windows-x64` 是不编译 Compatibility / Oracle 的纯 Dillen Preset；兼容原型必须通过显式 `dillen-compatibility-windows-x64` Preset 启用，不得成为 Standalone 的隐式依赖。

### 3.18 当前完成度

**已完成基础版**：

1. 统一 Mechanism / Entity / Component / Relation ID 与值类型；
2. Mechanism / Component / Relation Schema、Algorithm、Entity / Relation / Mechanism Definition、Spawn 和 Capability Registry；
3. Manifest、多 Package Source Layer、一层一 Package 所有权、自动 SHA-256 `content_digest` 验证、Package Lock、绑定 Package 身份的真实 Source Lock、Ruleset Fingerprint 和完整性验证；
4. Runtime Compiler、Ruleset 传递依赖闭包裁剪、Slot 化布局和 Frozen Runtime Catalog；
5. Authoritative World、Entity / Component / Relation / Mechanism Store；
6. WorldBuilder 显式 Spawn；
7. Lifecycle、Algorithm Create / Tick / Event / Command；
8. 跨 Store World Transaction、Command Queue、Fact Stream；
9. 权威 Scheduled Inbox、RNG Stream、稳定排序和 Snapshot；
10. Root / Extension Ruleset 组合、保护策略和 Fingerprint；
11. 外部 Package / Capability / Component / Entity / Relation / Mechanism / Algorithm / Definition / Spawn / Ruleset Authoring 闭环；
12. Entity / Component / Relation / Mechanism 同代际不可变 Query Snapshot 与稳定获取接口；
13. 外部 Declarative Program、Query / 字段 / Event / RNG 条件、通用事务指令、Definition 专属 Slot Bytecode、内建无循环 VM 与事务输出闭环；
14. 受控 Script 已完成外部语法、加载时 Slot 编译、确定性字节码 VM、指令边界抢占、权威状态/Continuation 事务提交、内存配额、Save 与 Replay 契约；并已与 Declarative 后端在**运行期与加载期依赖闭包**上等价——`Transact` 指令内嵌 `AlgorithmInstructionDefinition`，经共享 `EmitBytecodeTransaction` 执行，`BuildCompileSelection` 同时遍历 script 阶段，支持全部通用事务指令与 `when` 条件；
15. Destroy、确定性指令预算、非权威墙钟诊断、单实例 Fault 隔离、三种失败策略和显式恢复；
16. 全权威状态 Canonical Save / Load、原子恢复、显式 Schema Migration、固定 Command Log Replay 与稳定 Checksum；
17. 最小 Standalone Host、外部 Authoring Session 启动、交互/脚本化 CLI Inspector、即时与排队 Command、状态查询及原子 Save / Load 文件闭环；
18. Windows x64 纯 Dillen 测试清单在 Demo 0.5 封存时为 33 项；截至 2026-09-03，启用 Presentation 与 GL 地图后端的 `dillen-map-renderer-windows-x64` 配置已在 Debug / Release 均通过 **43/43**。`thread_contract_probe`（派发顺序无关性）、`controlled_script_probe`、`projection_adapter_probe`、`mechanism_ids_probe`、`capability_invocation_probe`、`scale_probe`、`demo_0_5_vertical_slice_probe` 与 `architecture_guard_probe` 分别固化 Script 沙箱/持久化、Projection 身份/迁移、Stable Identity 冻结哈希、Capability 契约调用、代表性规模、正式玩法纵向切片，以及模块分层依赖与“无 HOI3/oracle include”的源码级门禁；
19. Kernel 工程验证夹具已以聚落增长与贸易周期两个外部机制包、均衡/加速两个可替换 Root Package / Source Layer 完成端到端验收，并固化闭包裁剪、源摘要篡改拒绝、存档恢复、确定性回放、Source Lock 篡改拒绝与跨 Root 读档拒绝；`dillen_demo_1_0` 只是历史目录名，不代表未来产品里程碑。
20. Capability 级 **fire-and-forget 闭环**收口：`provides_capabilities` + `invoke_capability` + `payload / payload_from` + `capability_invoked` + `from_payload` + 定向单提供者（`target_role`）+ 显式版本协商（限定在 Package Lock 声明的提供集内）+ Controlled Script 与 Declarative 在运行期和加载期闭包上等价；跨机制交互不引用对方 Mechanism Type / Instance ID。Demo 0.5 已用生产报告、科研拨款和科技解锁三条真实 Capability 边完成闭环。**已确认的 v1 边界**：单向链路足够。同 Tick 多发送者写入同一接收实例的丢失更新**已于 2026-09-01 闭合**——不是靠改 ABI，而是靠 `MechanismAddFieldOperation`（内层 tag 7，纯加法）：命令携带增量而非绝对值，由 Executor 对已提交值做读改写，**已冻结的 v1 命令布局一个字节未动**。详见 §4.3 第二轮审查修复第 1 条。仍属后续纯加法能力的是：多 Operation、返回值、关联 ID。

**本轮已补齐的核心缺口**：

1. 受控 Script 的内存配额、指令边界可抢占沙箱、权威 Continuation 与持久化状态；受控 Script 现已能做全部通用事务（Query / Event / Command / Capability）+ `when` 条件，与 Declarative 后端共享编译下降形态与运行期执行；
2. Capability 调用 ABI 收口：定向单提供者、显式版本协商、Controlled Script 同等访问；
3. External Corpus Adapter 恢复所需的 Projection Artifact 联合身份、内容封印、Projection Lock 与 Adapter Migration。

**当前主线缺口（按执行顺序）**：

1. **通用 Gameplay Authoring 表达力**——补齐构造科研、生产、外交、政治、情报和战区参考机制所需的通用 Schema、Definition、Relation、Query、Event、Capability 与事务组合能力；任何缺口必须先证明可跨领域、跨 Ruleset 复用。
2. **静态定义系统**——让外部静态规则与类型定义经过 Schema、Parser、Resolver、Definition Registry、引用校验、Package / Source Lock 和 Runtime Freeze，成为可查询、可版本化、不可在 Tick 热路径临时解释的只读数据。
3. **Trigger / Effect / Scope 前端与语义层**——建立 Dillen 原生的类型化 Scope 上下文和遍历、无副作用 Trigger 谓词、只产生事务的 Effect，以及共享的 Registry、Resolver、Compiler、诊断和 Frozen Program 表示。
4. **六领域参考 Package 验收**——以科研、生产、外交、政治、情报和战区六类独立 Package 验证通用能力，而不是把这些名词写进 Kernel；跨机制交互只经过公共 Contract。
5. **External Corpus 接入基础**——在纯 Dillen 能力验收通过后，完成 Normalized IR 容器、Importer / Mapping 执行分离、Projection Lock 与 Ruleset 身份接线，再开始 HOI3 正式移植。
6. **后续性能能力**——同相位 Worker Pool、1-vs-N 对拍、Native Executor `parallel_safe` 契约、更大 Package 图加载基线和粗粒度 CoW 优化均排在语义完整性之后；不得以性能优化替代语义门禁。

地图纵向切片仍有 14187 Entity Save / Load、Migration 后续跑、长周期 Tick、多检查点 Replay、GUI 关闭/重建和窗口 Host/无头路径对拍等耐久性待办，但这些项目自本次路线重置起进入**非阻塞维护清单**，不再排在通用 Kernel / Authoring 能力之前，也不产生新的产品 Demo。

Capability ABI v2 的多 Operation、返回值和关联 ID 仍是可选增量；除非六领域参考机制证明 v1 无法表达必要的跨机制契约，否则不进入当前主线。

**在第 1—5 项通过前继续暂停**：

- HOI3 Importer 新语义切片；
- HOI3 Mapping Profile；
- HOI3 Runtime WorldBuilder；
- Oracle 横向逆向扩展；
- 以 HOI3 War / Diplomacy 直接代替纯 Dillen 参考 Package 验收；
- 在 Kernel 中添加 Research、Production、Diplomacy、Politics、Intelligence 或 Theater 专用类型、Query、Command、Effect 或 Setter。

### 3.19 工程化加固与代码修复

本节记录的工程化加固不改变任何 Kernel 边界、权威状态所有权、依赖方向或 Ruleset 语义；这些改动在各自落地时均通过当时的纯 Dillen 测试。当前工作区整体门禁状态必须以 §3.9 和 §4.1 顶部的最新实测为准。

**构建与工程基线**：

- MSVC 构建启用 `/W4 /permissive-`；`std::visit` + `if constexpr` visitor 尾部 `return` 的 C4702 误报以 `/wd4702` 定点关闭并注释说明。当前标准核心在此配置下 0 warning。
- `cmake/DillenTargets.cmake` 的非 MSVC（GCC / Clang）分支：`-Wall -Wextra -Wpedantic -Wno-missing-field-initializers`。最后一项是定点关闭，理由与 `/wd4702` 同类：本项目通行写法是只花括号初始化 POD 的前若干成员、其余交给值初始化（`MechanismReference{kind, type, value}`、各 `WorldCommand` 工厂等），标准保证省略项零初始化，正是这些调用点想要的；逐一写全会让"给结构体加一个成员"变成全仓改动。MSVC 不报此警告。
- **跨平台移植已完成并转为阻塞门禁（2026-08-30）**：本机 WSL Ubuntu 24.04 当时以 19 项清单实测，两个编译器均 **0 warning、19/19 probe 全绿**；当前 CMake 清单为 26 项，CI 的 GCC / Clang 分支会运行与 Windows 相同的 26 项门禁，本轮未在本机重复 Linux 实测。
  **本地预检与 CI 镜像的差异要说准**（2026-08-31 更正）：**编译器完全一致**——GCC 13.3.0 与 Clang 18.1.3，这是唯一可能改变字节的一层；**构建工具并不一致**——镜像是 CMake 3.31.6 / Ninja 1.13.2，本机是 3.28.3 / 1.11.1，两者只管编排、不进产物。此前本条写作"CMake 3.28 / Ninja 1.11 与镜像同代"，是错的。另需记住：本地预检**永远跑不到** `actions/checkout` 的 `sparse-checkout`，因此"构建是否引用了 `Project-Dillen/` 之外的文件"这类问题只有真实 CI 能发现。`.github/workflows/ci.yml` 的 Linux job 已移除 `continue-on-error`，与 Windows MSVC job 同为必过门禁。这条门禁的实质作用是防止 Package `content_digest` 再次跨平台漂移（见下条 `.gitattributes`）。
- `.github/workflows/ci.yml`：Windows MSVC 跑 `dillen-standalone-windows-x64` preset 的 configure / build / ctest；Linux 跑 gcc 与 clang 两个 matrix 分支的 Ninja Debug configure / build / ctest。两者均为阻塞门禁。
- 仓库卫生：新增 `.gitignore` / `.editorconfig`，停止跟踪约 1790 个构建产物、`.vs/` 与本地日志；新增 `Project-Dillen/README.md`、根 `CONTRIBUTING.md`。引擎子树（`Project-Dillen/`）以 MIT License 发布（`Project-Dillen/LICENSE`）。
- 新增 `architecture_guard_probe`（不链接任何引擎库，直接读源码）：遍历 8 个 Standalone 模块（kernel / world / runtime / persistence / parser / authoring / adapter / host）的每个 `.hpp` / `.cpp`，把每条带引号 `#include` 解析回所属模块，任一跨层依赖（超出该模块 CMake `PUBLIC_LINKS` 允许范围）或引用非 Standalone 子系统（`hoi3` / `oracle` / `compatibility` 路径 token）即以 `file:line` 失败。首次把"kernel 头文件不含 HOI3 类型""world 不反向 include runtime"从人工 grep 变成 CI 门禁，是 Demo 0.2（§4.2）架构诊断验收项的落地件。已用注入-回滚自测确认能捕获违规。

**正确性与健壮性修复**：

- 事务执行器调度事件时，`std::find_if` 结果先判 `end()`，找不到即拒绝事务，消除可能的迭代器越界解引用。
- Authoring 的 `Declare` / `Resolve` 与 `ValidateAndCompile` 一致，按 `CatalogDisposition::Active` 过滤 —— 落败的 Replace-Path 工件不再进入 Declare / Resolve。
- Declarative 程序中目标 Entity 无法解析的 `set_component_field`，在编译期与 `create_entity` / `spawn_mechanism` 一样硬失败，而非静默跳过。
- `schedule_event` 的 `delay == 0` 在加载期即拒绝（运行期必然被 `dueTick <= currentTick` 拒绝）。
- Save Codec 的 `String()` / `Raw()` 长度检查前置 `offset_ > limit_` 守卫，即使内部不变量被破坏也返回失败而非越界读。
- Runtime Compiler 编译 Spawn 时对 `FindLayout` 结果补 `nullptr` 检查，与 Definition / Entity-Component 路径一致。
- **（审核修复）`BuildCompileSelection` 依赖闭包只遍历 `algorithm->program.stages`**：Controlled Script `Transact` 里的 Capability / Entity / Spawn 引用不进闭包，可能不进 Frozen Catalog。已抽 `closeOverInstruction` lambda，同时遍历 `algorithm->script.stages`。
- **（审核修复）`invoke_capability` / `provides_capabilities` 的版本解析扫描整个 Contract Registry**：绕过 Package Lock，Kernel 编译不自封闭。`ResolveCapabilityVersion` 改为只在 `packageLock.Entries()[].providedCapabilities` 里选版本（镜像 `RuntimeCapabilityResolver::Resolve`）；提供契约的包 manifest 须 `provides = { capability = { name version } }` 声明。
- **（二轮审核修复）跨 Package 的契约版本串绑**：`ResolveCapabilityVersion` 在**全部**锁定包中取最高版本，而 Definition / Algorithm 没有 PackageId，无法判定归属——两个包分别提供 v1/v2 时，某个 Definition 可能绑定到另一个包声明的版本。新增 `RejectAmbiguousCapabilityProviders`：一个契约身份被一个以上锁定包提供即编译拒绝。
- **（二轮审核修复）行尾未固定导致 `content_digest` 跨平台不一致**：仓库无 `.gitattributes` 且 `core.autocrlf=true`，Git 存 LF、Windows 工作树是 CRLF。`content_digest` 是源文件**原始字节**的 SHA-256，所以同一个 commit 在 Windows 与 Linux 上会算出两个不同的摘要，其中一侧必然被判为篡改。（Linux CI job 标了 `continue-on-error`，一直掩盖着这个问题。）新增 `.gitattributes`，对全部 Dillen Authoring 扩展名与命令流 `.txt` 固定 `text eol=lf`，并把工作树里残留的 CRLF 源文件转为 LF、重算受影响的 Package 摘要。这不是风格问题，是确定性正确性问题。
- **（Demo 0.2 冻结加固）磁盘 variant tag 无保护**：`MechanismCommandOperation`（8 项）、`WorldCommandPayload`（11 项）、`WorldEventPayload`（18 项）三个 variant 的 `.index()` 被直接当作 tag 写入存档与 Fact Stream，但没有任何机制阻止重排。中间插入或重排即静默重写全部存档并移位全部 Replay Checksum。已加 `VariantAlternativeIndex` 与 36 条逐项 `static_assert` + 3 条 `variant_size_v` 断言；三个定义处加 `FROZEN ORDER` 注释。末尾追加仍合法，其余改动编译期即停。详见 §4.2。

**Stable Identity 层重构**：

- 21 个手写 ID / Slot 类型（各含 `struct` + `operator bool` + `== / != / <`）收敛为单个 `StrongId<Tag, Underlying, Empty>` 模板加 21 行 `using` 别名。类型仍是聚合体，`Id{}` / `Id{rawValue}` / `return {rawValue};` / 公开可变的 `.value` 全部不变。
- 21 条 `static_assert` 锁定每个 ID 的 `sizeof == 底层类型`、`alignof` 一致、trivially-copyable、standard-layout。
- 新增可选的 `std::hash<StrongId<…>>` 偏特化，不改动任何现有容器，仅为将来把热点有序查找替换为哈希查找解除障碍。
- 所有 `Stable*Id` 哈希函数、归一化规则与哈希域字符串一字未动；`mechanism_ids_probe` 以冻结的十六进制期望值锁定全部 17 个 `Stable*Id` 输出，`persistence_replay_probe` / `dillen_demo_1_0_probe` / `runtime_catalog_probe` 验证 Ruleset Fingerprint、存档字节与 Replay Checksum 逐字节不变。
- 新增 `mechanism_ids.natvis` 供 VS 调试器显示裸 ID 值。

新增一个 ID 类型现在是 1 行 `using` + 1 行 `static_assert`，不再需要修改多处 Kernel 样板。

**运行期共享层（9c）**：

- 新增 `runtime/bytecode_transaction.{hpp,cpp}` —— 从 Declarative VM 抽出 `EvaluateBytecodeConditions` 与 `EmitBytecodeTransaction`（全部 13 个字节码 opcode → World 命令）。Declarative VM 重写为循环调用这两个函数；Controlled Script VM 的新 `Transact` opcode 也调它们。两个后端的运行期行为从此不可能漂移。
- 从 `IsValidAlgorithmProgram` 抽出共享的 `IsValidAlgorithmInstruction`（`algorithm_program.hpp`），供 Declarative 与 Controlled Script 两个程序校验器复用。
- 编译期未做物理抽取：Controlled Script 的 `Transact` 下降用 `runtime_compiler.cpp` 内一个 `[&]` 捕获全部的 `lowerTransact` lambda，与 Declarative 下降镜像。这是刻意的——冻结前不对编译器最密的 ~350 行下降逻辑做整体搬运。运行期共享已消除行为漂移风险，编译期只可能在"先报哪条诊断"上无害地不同。
- 全部改动零 Save 格式变化、零既有测试回归；`declarative_algorithm_vm_probe` / `algorithm_runtime_probe` / `controlled_script_probe`（已扩展）与 `dillen_demo_1_0_probe` 逐字节一致。

### 3.20 Capability 调用 ABI 与性能现实检验

**Capability 级 fire-and-forget 闭环**：详见 §3.8（含"尚不是完整通用 ABI"的边界说明）。核心：跨机制交互从"按对方 Mechanism Type 名查询数量"改为"向契约的提供者（广播或定向单个）发一次版本协商过的单向调用"。落地面：

- Kernel：`AlgorithmInstructionKind::InvokeCapability` 与字节码 opcode；`InvokeCapabilityCommand`（`WorldCommandPayload` variant 与 Save Codec 索引 10，v5 加 `targetInstance` + `capabilityVersion`）；`CompiledMechanismDefinition.providedCapabilities`；`CapabilityProvisionDeclaration`（契约名 + `CapabilityVersionRange`）；`CapabilityDeliveryEventType()` 派生事件类型；`operandFromPayload`、`targetRoleName` / `targetRoleSlot`、`capabilityVersions` / `capabilityVersion` 字段；`ResolveCapabilityVersion` 区间内取最高。
- World：`WorldTransactionExecutor` 按稳定 Instance ID 顺序扇出——广播时命中 `provides_capabilities` 且版本相符者投递，定向时只投给 `targetInstance` 且未命中即 `Failure`；扇出循环在确定性 Executor 里，不在 VM 里。
- Runtime（9c，两个 VM 共享）：`runtime/bytecode_transaction.{hpp,cpp}` 的 `EmitBytecodeTransaction` + `EvaluateBytecodeConditions` 是全部 13 个字节码 opcode → World 命令的唯一实现，Declarative VM 与 Controlled Script VM（`Transact` 指令）都调它——含 `InvokeCapability`（`target_role` 角色 Slot 读取）、`from_payload` 操作数替换、字段读改写、生命周期门。共享 `IsValidAlgorithmInstruction`（`algorithm_program.hpp`）供两个校验器复用。
- Parser：`invoke_capability`（`capability delay priority payload target_role version`）、`capability_invoked` 条件（`ScheduledEventTypeEquals` 语法糖）、`provides_capabilities`（裸名或 `requirement` 范围块）、`from_payload = yes`；`script` 阶段的通用事务指令与带 `when` 的字段指令解析为 `Transact`。
- Compiler：`invoke_capability` 请求版本区间必须与组合 Ruleset 中某契约相交（否则编译诊断）、`delay` 为正、`target_role` 必须是调用方 Definition 的角色 Slot；`provides_capabilities` 解析为具体契约版本并入闭包。
- Save v5：`InvokeCapabilityCommand` 加两字段；投递仍复用既有 Scheduled Event 的 `type` 字段，Scheduled Event 存档格式本身不变；无 4→5 迁移器（无线上 v4 存档含该命令）。
- 历史 `dillen_demo_1_0` 工程夹具中的两个机制包已删除 `query_at_least { type = dillen.demo1.<对方> }`，改为 `dillen.demo1.market_pressure` 契约。契约现在住在**中立的 `dillen.demo1.contracts_package`**（`packages/contracts/`，首个走通完整 Authoring 管线的 `.dcapability`），聚落包与贸易包**各自依赖它、互不依赖** —— 提供者实现从此可被替换。

**性能现实检验（`scale_probe`）**：合成世界 N 个同类型机制实例 × M 个 Tick，每 Tick 每实例 `add_field counter 1`，验证 M Tick 后全部实例 counter 精确等于 M，并打印各阶段耗时。

**优化前基线（Debug）**：250×10 ≈ 3 s（246 ms/Tick）；400×20 ≈ 11 s（565 ms/Tick）；**2000×60 在 5 分钟内未跑完**。耗时随实例数**约二次增长**。

**优化后（2026-08-30，同机同构建）**：

| N（实例） | ms/Tick |
| --- | --- |
| 250 | 9.8 |
| 500 | 18.0 |
| 1000 | 34.9 |
| 2000 | 78 |
| 4000 | 154 |

**N 翻倍 → 耗时约翻倍，二次项已消除。** N=250 从 246 → 9.8 ms/Tick（**25×**）；**2000×60 从"5 分钟未跑完"变为 4.75 s**。

两处改动，收益比例与最初判断**不同**（原判断认为逐事务 Store 深拷贝是主因，实测不是）：

1. **分相位批量提交（`WorldTransactionBatch`）**：一个 Tick 相位只拷贝一次 6 个 Store、按稳定顺序把 N 条事务应用到同一份工作副本、相位末尾只发布一次快照。**实测贡献约 5.6× 常数因子，但不改变增长阶。**
2. **`DispatchEvent` 资格判定外提（真正的二次项）**：事件广播是 `for(event) for(instance) dispatch()`，而"该实例的算法有没有 Event 入口"的判断在**内层**。`CaptureAlgorithmEvents` 每条已提交事务产生一条事件，于是每 Tick 有 N 条事件 × N 个实例 = **N² 次 catalog 查找**——即使世界里根本没有任何 Event 阶段算法也照跑。把资格判定提到循环外、先构建一次广播目标列表后，N=1000 时该段从 **1353 ms → 0.3 ms**。快照顺序不变，故调用顺序与逐对过滤逐字节一致。

**提交语义验证**：批量路径是**乐观**的，只在"全相位干净"时生效——任一 invocation 失败、目标在相位中途被销毁/隔离、或任一事务被拒，立即**整批丢弃**（工作副本析构，权威世界从未被触碰、未消耗任何 Sequence、未发布任何事件、未修改任何 invocation 记录），退回原有逐事务路径重跑整个相位。因此失败路径行为与批量化之前**逐字节相同**。已保持不变的量：`world_.revision_`（进存档，按"每条产生变更的事务 +1"累加）、Command Sequence 取值与顺序、`world_.tick_`、事件顺序。唯一变化是相位内不再逐事务重发布 Query Snapshot（`publication` 不进存档，测试只断言其单调性）；相位内的"目标是否可用/已隔离"改为读批次工作副本，与原先逐事务重发布后的查询等价。空相位保持完全空操作（惰性建批，不拷贝、不发布）。

**验证**：Windows MSVC / Linux GCC / Linux Clang 三者 0 warning、19/19 全绿（当时的项数；当前清单为 26 项）；`persistence_replay_probe`、`dillen_demo_1_0_probe` 的存档字节与 Replay Checksum 不变。

**仍未做**：粗粒度 CoW（6 个 Store 各 `shared_ptr<const Data>` + 写时克隆）与同相位算法并行。二者现在都是**纯常数因子**优化，不再阻塞规模——每 Tick 的剩余成本已是线性的快照发布与 VM 执行。

**剩余的两项（现在都只是常数因子，不再阻塞规模）**：

1. **粗粒度 CoW**：6 个 Store 各自 `shared_ptr<const Data>` + 首次写时克隆（`use_count() > 1` 才 clone）。批次构造的那一次 O(世界大小) 拷贝变 O(1)；只写 `mechanisms` 的事务不再拷另外 5 个 Store。`RuntimePersistenceService` 的 friend 直连成员访问需改为走 `Data` 存取器。风险不低（≈150–200 处机械改写，每处读/写路径必须分清 `data_->` 与 `mut()`，错一处即破坏确定性），而收益现在只是常数因子——**建议放到契约冻结之后**，因为它不改变任何对外语义。
2. **同相位算法并行**：`DispatchTick/Create/Event/Command/Destroy` 对每个实例跑 VM（只读快照进、事务出，实例间无依赖）。用简单线程池 / `for_each(par)`，结果按 Instance ID 稳定排序后再串行应用——确定性由构造保证。设实例数阈值，低于阈值仍单线程。此项**会确立"Kernel 可使用工作线程"这条契约**，宜在冻结前定，即使实现延后。

---

## 4. 开发顺序与能力验收计划

### 4.0 路线重置（2026-09-03）

此前以 Demo 0.8 → Demo 1.0 → Demo 1.1 → External Corpus Demo 编号推进的计划自本次修订起**全部撤销**。原因是地图、窗口或内容数量无法证明 Kernel 已具备承载完整大战略语义的通用表达力；继续按产品 Demo 堆叠界面和玩法，会在 Trigger / Effect / Scope、静态定义和跨领域语义尚未成型时过早冻结错误接口。

以下历史成果继续有效，但不再产生后续版本编号：

| 历史成果 | 当前定位 | 状态 |
| --- | --- | --- |
| **Demo 0.2 — Kernel Contract Freeze** | Stable ID、冻结契约和架构门禁的历史基线 | **已封存**，标签 `demo-0.2-contract-freeze` |
| **Demo 0.5 — External Mechanism Vertical Slice** | 经济—科研—生产最小闭环的持续回归样本 | **已封存**，持续回归 |
| **地图 / Presentation 纵向切片（原 Demo 0.8）** | 地图导入、权威实体映射、窗口、输入、Presentation Compiler 与机制 UI 的基础设施验证 | **冻结候选**；耐久性收尾转入非阻塞维护清单 |
| **`dillen_demo_1_0` 工程夹具** | Kernel Verification Fixture，不是产品 Demo | **已完成**，保留历史目录名并持续回归 |

在通用 Gameplay 语言和六领域参考机制完成以前，不再设立新的产品 Demo 编号或目标日期。新的里程碑只按“能力是否完整、是否可由外部 Package 使用、是否通过确定性与持久化门禁”裁定。

### 4.1 当前唯一主线

当前严格按以下顺序推进：

1. **通用机制表达力审计**：分别设计科研、生产、外交、政治、情报和战区的外部机制模板草案，用它们发现 Kernel 的通用原语缺口；草案不得作为 Kernel 类型来源。
2. **静态定义前端**：建立外部 Definition Schema、类型化字段、嵌套值/集合、枚举或符号、稳定引用、跨文件 Declare / Resolve / Validate、覆盖诊断、版本和冻结目录。
3. **Scope 语义层**：建立类型化当前上下文、命名角色、关系遍历、集合选择、父子上下文和确定性排序；Scope 只解析“对谁求值或执行”，不包含业务动作。
4. **Trigger 语义层**：建立无副作用谓词、组合逻辑、比较、存在/全称/计数、集合归约、Definition 与 Snapshot 查询；所有读取必须来自同代际 Query Snapshot。
5. **Effect 语义层**：建立只生成 Command / World Transaction / Event / Capability Invocation 的类型化效果程序；禁止直接写 Store，禁止把领域动词编码进 Effect VM。
6. **共享语义编译管线**：`Source → AST → Semantic IR → Resolve → Validate → Slot/Stable ID Compile → Frozen Program Catalog`；Declarative 与 Controlled Script 必须复用同一语义和事务下降路径。
7. **六领域参考 Package 验收**：科研、生产、外交、政治、情报和战区各自形成 Contract / Mechanism / Content 测试包，并通过跨机制契约、故障隔离、Save / Load、Migration、Replay 和更换 Root 验收。
8. **HOI3 移植准备**：完成独立 Importer ABI、Normalized Source IR、Mapping Profile Compiler、Projection Lock 和 Adapter Migration 的执行闭环。
9. **HOI3 正式移植**：按静态定义 → 历史与初始状态 → Trigger / Effect / Scope → Event / Decision → 机制与 Presentation 映射的顺序推进；HOI3 源语义不得反向成为 Kernel 业务类型。

第 1—7 项是 HOI3 正式移植的硬前置。某个 HOI3 文件“已经可以被词法解析”不等于 Dillen 已经具备承载其 Gameplay Meaning 的能力。地图纵向切片的剩余耐久性验证只作为维护任务穿插执行，不得阻塞或改变上述顺序。

**下列内容是已经完成的基础建设记录，不表示当前待执行队列**：

1. **Root Ruleset 收口（已完成）**：已移除全局不可替换 Core Ruleset 假设，完成显式 Root 选择、纯加法 Extension Composition、保护策略、确定性排序和 Fingerprint；后续 Override 只能在独立授权模型完成后增量加入。
2. **外部 Authoring 纵向管线（基础闭环已完成）**：Package Manifest、Capability Contract、Component Schema、Entity Definition、Relation Schema / Definition、Mechanism Template、Algorithm Descriptor、Mechanism Definition、Spawn、Root Ruleset 和 Extension Ruleset 已能从多个 Source Layer 进入 Registry；每层严格绑定唯一 Package，真实 Source 自动计算并验证 Package SHA-256 摘要，绑定 Package 身份的 Source Lock 已进入编译、Fingerprint 与持久化身份。只有组合 Root 的依赖闭包进入 Frozen Catalog。复杂值和面向作者的工具链作为后续增量能力补齐。
3. **通用 Query 完整化（已完成）**：已发布 Entity / Component / Relation / Mechanism 同代际不可变快照、稳定索引和跨发布代际安全句柄；增量快照属于后续性能优化，不再阻塞 Query 核心契约。
4. **可执行 Algorithm 后端（Declarative 与 Controlled Script 基础闭环已完成）**：外部 Declarative Program 已能编译 Query、字段、Scheduled Event 与 RNG 条件，以及 Entity / Component / Relation / Mechanism / Event / RNG 通用事务指令为 Definition 专属 Slot / Stable ID Bytecode，并由内建无循环 VM 生成 World Transaction；Controlled Script 已提供类型稳定的持久状态、跳转、条件、`yield/halt`、确定性切片抢占、内存配额和 Save / Replay 状态契约。Native 后端继续受显式 Executor Registry 与协作 Budget 约束。
5. **生命周期和 Fault 收口（已完成）**：Destroy、正数确定性指令预算、单实例权威 Fault 隔离、`isolate / pause / fail` 策略、显式恢复、引用保护和定向 Inbox 清理均已接入统一事务；墙钟阈值已从权威确定性结果中剥离，只产生 Invocation 诊断。Native C++ 回调不执行不安全的线程强杀，未来 Host Watchdog 或可抢占 Worker 也不得把墙钟结果回写为 Gameplay 状态。
6. **Persistence / Migration / Replay（已完成）**：已保存四类权威 Store、算法状态、Clock、RNG、Inbox、Queue、创建序号和稳定 Sequence；完成 Canonical Binary Codec、身份/版本拒绝、候选世界原子恢复、显式 Schema Migration、派生索引重建与固定 Command Log 的双次确定性回放。
7. **Standalone Host（已完成）**：已提供纯 Dillen `project-dillen` CLI、外部 Authoring Session 启动、Query 状态检查、即时/排队 Command、Tick 驱动、脚本化命令流和原子 Save / Load 文件闭环；窗口后端属于后续 Platform 增量，不再阻塞 Host 核心契约。
8. **Kernel 工程验证夹具（已完成；此前误称"纯 Dillen Demo 1.0"，见 §4.0）**：聚落增长与贸易周期两个外部机制包已通过 Package Lock 和真实 Source Lock 进入 Standalone；均衡/加速 Root 各自作为正式 Root Package，可在不重新编译引擎的情况下替换，并产生不同 Fingerprint、Spawn 组合和权威初始状态。独立 Probe 已固化 Query、Scheduled Event、RNG、Capability 契约调用、通用事务、Ruleset 闭包裁剪、Package 源摘要篡改拒绝、Save 恢复、双次 Replay、Source Lock 篡改拒绝与跨 Root 读档拒绝。
9. **Capability 级 fire-and-forget 闭环收口（见 §3.8 / §3.20）**：`provides_capabilities` + `invoke_capability` + `capability_invoked` + `from_payload` 让两个独立机制包仅通过契约交互。本轮补齐 **9a 定向单提供者**（`target_role` → 角色 Slot → 单实例投递）、**9b 显式版本协商**（加载期在 Package Lock 提供集内解析为具体版本；Save 4→5）、**9c Controlled Script 与 Declarative 在运行期 + 加载期闭包上等价**（`Transact` + 共享 `EmitBytecodeTransaction` + 共享 `IsValidAlgorithmInstruction`；`BuildCompileSelection` 遍历 script 阶段；无 Save 格式变化），并把 Demo 契约移入中立 `contracts_package` 使两个机制包互不依赖。**剩余**：通用多 Operation ABI（= Capability ABI v2，不阻塞 Demo 0.2，见 §3.8 / §4.2）。
10. **性能现实检验与运行时优化（主体已完成，见 §3.20）**：`scale_probe` 原测得 Tick 耗时随实例数二次增长（250×10 = 246 ms/Tick，2000×60 五分钟未跑完）。已实施**分相位批量提交**（`WorldTransactionBatch`，一个相位一次 Store 拷贝、一次快照发布，乐观路径失败即整批丢弃退回逐事务慢路径）与 **`DispatchEvent` 资格判定外提**（事件广播的 N² catalog 查找 → 一次目标列表构建，N=1000 时该段 1353 ms → 0.3 ms）。结果：**耗时随实例数线性**，250×10 = 9.8 ms/Tick（25×），2000×60 = 4.75 s。剩余粗粒度 CoW 与同相位并行只是常数因子。
11. **External Corpus 接入身份基础（已完成一部分）**：已用合成 Projection 固化 Corpus / Importer / IR / Mapping / Target / Generated Source 联合身份、篡改拒绝和唯一迁移链；在六领域参考 Package 验收后，仍须用合成 Corpus 完成 Importer / Mapping 执行分离与 Projection Lock 接线，随后才能恢复 HOI3 正式移植。

### 4.2 Demo 0.2：Kernel Contract Freeze

**目标日期：2026-09-30**

范围：

- Root Ruleset / Extension Ruleset 语义定稿；
- Kernel、World、Runtime、Parser Target 不依赖 HOI3 Adapter；
- World Transaction、Inbox、RNG、Scheduler 和 Snapshot 契约冻结；
- **线程契约冻结**（只有算法派发可并行，提交/标识分配/发布/RNG 推进永远单线程；见 §3.9）；
- **Capability 调用 ABI 冻结为 fire-and-forget v1**（见下方“冻结范围界定”）；
- 形成最小外部 Package Fixture；
- 架构诊断能够阻止非法依赖和缺失 Contract。

验收：

- `DILLEN_BUILD_HOI3_COMPATIBILITY=OFF` 时 Standalone 构建和核心测试通过；
- Kernel 公共头文件不包含 HOI3 类型；
- Root Ruleset 可以被另一份测试 Root Ruleset 替换；
- 同优先级 Command / Event 使用稳定 Sequence。

**当前状态（2026-08-31）：范围与验收全部满足，冻结动作已执行，标签 `demo-0.2-contract-freeze` 已打。**

（此前本节存在自相矛盾的表述——一处写"冻结暂缓到第 9、10 项收口后"，后文又写"交付物已完成"。那是分阶段推进过程中的中间态遗留，现统一为：**前置项已收口，冻结已生效**。）

范围七项逐条落实：Root/Extension 语义定稿见 §4.1 的已完成建设记录第 1 项；无 HOI3 依赖与架构诊断由 `architecture_guard_probe` 源码级门禁守住；最小外部 Package Fixture 见下文重新分类的工程验证夹具；World Transaction / Inbox / RNG / Scheduler / Snapshot 契约、线程契约、Capability fire-and-forget v1 三项冻结均已由下方“实际交付物”落为可执行门禁。当前 `dillen-standalone-windows-x64` Debug / Release 各 26/26，Linux gcc / clang 各 Debug / Release 配置为同一 26 项阻塞门禁；balanced/accelerated 可换 Root；稳定 Sequence 见 §4.1 的已完成建设记录第 6 项。当前工作区门禁状态以 §3.9 和 §4.1 顶部记录为准。

冻结所依赖的前置内容：

- **9a 定向单提供者 —— 已完成**：`InvokeCapabilityCommand` 加 `targetInstance`，`target_role` → 角色 Slot；`capability_invocation_probe` 有完整 e2e（两个 sink 提供同一契约，pump 角色绑定其一 → 只有被点名者收到；去掉 `target_role` 该用例即失败，判别力已验证）。
- **9b 显式版本协商 —— 已完成**：`capabilityVersion` 进命令与 Save Codec（4→5），编译期在 Package Lock 提供集内解析版本，并拒绝一个契约被多个包提供的歧义。
- **9c Controlled Script 等价 —— 已完成**：`Transact` 指令 + `runtime/bytecode_transaction.{hpp,cpp}` 共享运行期 + 共享 `IsValidAlgorithmInstruction` + `BuildCompileSelection` 遍历 script 阶段；无 Save 格式变化。
- **Demo 提供者解耦 —— 已完成**：中立 `dillen.demo1.contracts_package`（`demo/dillen_demo_1_0/packages/contracts/`）持有契约，聚落包与贸易包各自依赖它、互不依赖，提供者实现可替换。
- **§4.1 第 10 项 —— 主体已完成**：分相位批量提交 + `DispatchEvent` 资格外提，Tick 成本已从二次降为线性（§3.20）。提交语义变更已逐条验证：`revision` / Sequence / tick / 事件顺序不变，失败路径退回逐事务慢路径后逐字节相同，唯一变化是相位内不再逐事务重发布 Query Snapshot。
- **线程契约 —— 已定稿并已补齐结构（见 §3.9）**：只有算法派发可并行，提交 / 稳定标识分配 / 事件与快照发布 / RNG 推进永远单线程且按稳定 Instance ID 顺序。**契约随本次冻结生效，Worker Pool 实现延后**：实测派发仅占 Tick 的 ≈21%，完美并行上限 1.27×，因此实现顺序定为 **CoW → 并行**。
  **冻结后发现并已修正的两处问题**：（a）冻结当时契约描述的两相位槽位结构**并不存在**——六个 Dispatch 都在过滤循环里 `push_back`，执行顺序与槽位顺序焊死，根本无法并行；结构已于 2026-08-31 补齐，并由 `thread_contract_probe` 守住。（b）"所有算法派发都可并行"对 **Native Executor 不成立**——它是任意 `std::function`，`const` 不约束闭包捕获的共享可变状态；现已按后端分级：Declarative / Controlled Script 默认可并行，**Native 默认串行**，见 §3.9 的分级表。

**冻结范围界定 —— Capability 调用 ABI 冻结的是 fire-and-forget v1**：

本次冻结的 Capability 语义是**单向、单 Operation、无返回值、无关联 ID**的一次性调用，具体为：`provides_capabilities` 声明 + `invoke_capability`（`capability delay priority payload [target_role] [version]`）+ 加载期版本协商 + 广播/定向投递 + `capability_invoked` / `from_payload` 接收，以及 `InvokeCapabilityCommand` 在 Save v5 中的布局。**这一层就此定稿。**

`RuntimeCapabilityContract::operations` 字段虽已存在并受注册期校验，但未接入调用指令、World 命令或投递路由，**不属于本次冻结范围**。将来的多 Operation / 返回值 / 关联 ID 属于 **Capability ABI v2**，必须以纯加法方式引入（新指令或新命令 tag + Save 版本升级），不得改变 v1 的语义或既有存档布局。冻结 v1 不阻塞 Demo 0.2 —— 它冻的是一个已经端到端验证、已在 Kernel 工程验证夹具中承载跨包交互的完整闭环。**但要说清楚：夹具里的跨包交互是刻意最小化的。v1 是否够表达真实玩法纵深，要到 Demo 0.5 才有答案**（§4.3）。

**结论：冻结前置条件已全部满足。** World Transaction / Inbox / RNG / Scheduler / Snapshot、线程契约、Capability fire-and-forget v1 均可即刻冻结。剩余的粗粒度 CoW 与同相位并行都是**不改对外语义的内部优化**，不得作为冻结阻塞项；它们在冻结后实施时，验收标准是存档字节与 Replay Checksum 逐字节不变。

**冻结后的变更规则**：上述契约的任何语义变更一律走**纯加法**（新指令 / 新命令 tag / 新 Save 版本），不得改变已冻结语义或既有存档布局；需要破坏性变更时，先修订本节并记录理由。

**冻结动作的实际交付物**：Demo 0.2 不是"再做一个 Demo"——它门禁的**能力**内容已由 Kernel 工程验证夹具完成（§4.0）。它的交付物是**让"冻结"从备忘录里的一句话变成可执行的门禁**。

**（1）磁盘 variant tag 钉死 —— 已完成（2026-08-30）**。发现的隐患：`runtime_save_codec.cpp` 在三处将 `variant.index()` 直接当作磁盘 / 字节流 tag 写出，而这三个 variant **没有任何保护**：

| Variant | 备选项数 | 用途 |
| --- | --- | --- |
| `kernel::MechanismCommandOperation` | 8 | 存档中的 Mechanism 操作 tag |
| `kernel::WorldCommandPayload` | 11 | 存档中的 World 命令 tag |
| `kernel::WorldEventPayload` | 18 | Fact Stream tag（**即确定性 Replay Checksum 的输入**） |

在其中任一个 variant 的**中间插入或重排**备选项，会静默重写所有已有存档并移位所有 Replay Checksum——既不编译失败，现有测试也不一定能发现。`WorldEventPayload` 尤其隐蔽：这些字节**从不被读回**，但它们是 Replay Checksum 的输入。

已在 `runtime_save_codec.cpp` 内加入 `VariantAlternativeIndex` 辅助模板与 **36 条 `static_assert`**（每个备选项一条），再加三条 `std::variant_size_v` 断言捕获“新增了备选项却未钉位”。末尾追加仍然合法（只需补一条断言），其余改动一律在编译期停下。三个 variant 定义处各加了 `FROZEN ORDER` 注释，使约束在**编辑现场**即可见。

已做两组注入-回滚验证：交换两个备选项 → 精确点名两条失败；中间插入一个新备选项 → 点亮全部下游 6 条加 `gained an alternative`。MSVC / GCC / Clang 三平台 0 warning、19/19 全绿（当时的项数；当前清单为 26 项）。

**（2）黄金存档字节 —— 已完成（2026-08-30）**。上一项钉的是 tag 序号，这一项钉的是**字段顺序与编码**。均在 `persistence_replay_probe`：

- **规范世界黄金值**：存档 688 字节 / 校验和 `7194244525752032699`，回放 `finalStateChecksum` `9515266196334764553`、`factStreamChecksum` `14511951199989717232`。该世界含 Entity / Component / Relation / Mechanism / RNG Stream / Scheduled Event / 队列命令。
- **命令编码黄金值**（`CheckFrozenCommandEncoding()`）：手工构造一个 Save Image，其命令队列含**全部 11 种 `WorldCommandPayload` 备选项 + 全部 8 种 `MechanismCommandOperation` 备选项**（1 种在第一条事务里，其余 7 种在第二条），锁定 539 字节 / 校验和 `11380329816255759537`。
  **`add_field` 的语义已固定为增量**（2026-09-01）：`AddFieldComputed` / `AddIntegerConstant` /
  `AddDecimalConstant` 三个操作码一律发 `MechanismAddFieldOperation`，命令里只带 delta，
  由 Executor 对**已提交值**做读改写。`SetField` 仍是绝对写。这条区别不是优化而是正确性——
  绝对值是对 Dispatch 期快照算的，只要同一 Phase 有两个以上调用写同一字段就会丢更新。
  存档里因此会出现内层 tag 7；旧存档不含该 tag，读侧不受影响。

  **追加备选项必须同时在这里追加一条命令**：2026-09-01 追加 `AddField`（内层 tag 7）时，整套 26 个测试在黄金值一字未动的情况下全绿——因为黄金根本没编码过这个 tag。冻结面在新功能处开洞，与 2026-08-31 那次 `-Wswitch` 暴露的是同一类错误：**没有被构造过的备选项等于没有被冻结**。
  **覆盖范围（2026-08-31 补齐，2026-09-01 扩至 19 项）**：19 个备选项的**编码**由黄金字节数与校验和覆盖，**全部 19 个 tag 也已逐项往返对位**——外层 11 个 `WorldCommandPayload` 走 `commandQueue[0]`，内层 8 个 `MechanismCommandOperation` 走 `SetField`（第一条事务外层 tag 5）加 `commandQueue[1]` 的其余七个。
  此前内层只校验数量，"读写两侧同步漂移但字节数不变"这一类错误必然放行。补齐后用注入验证：读侧对调内层 tag 4/5（`ClearAlgorithmFault` 与 `Destroy`，两者均无载荷，故编码字节数与全部外层 tag 不变），被精确捕获。
- **这一条是被验证逼出来的**：最初只做了规范世界黄金值，注入"交换 `RngStreamAdvanceCommand` 两个相邻 U64 字段写出"后**没有被捕获**——因为那个世界的持久化命令队列里只有一种命令，`WriteWorldCommand` 的绝大多数分支从未被序列化。补上命令编码黄金值后重注入，即被捕获（字节数相同、**校验和不同** —— 两个指标都必要）。
- **跨平台**：上述全部黄金值在 Windows MSVC、Linux GCC、Linux Clang 上**逐位相同**，所以失配一定是真格式变更而非平台差异。失败信息直接写明"意外就修代码，有意就升版本+写迁移"。

**（3）已冻结契约面清单 —— 已完成（2026-08-30，2026-08-31 扩充）**：`Project-Dillen/FROZEN_CONTRACTS.md` 按存档与回放格式 / 稳定身份 / Capability ABI v1 / 线程契约 / 模块分层 / Authoring DSL 六类列出冻结项，**每一项标明由什么守卫**。第 0 节写明变更规则（纯加法允许；破坏性变更需升版本 + 迁移 + 修订 §4.2 + 更新黄金值；禁止为了让构建变绿而重置黄金值）。多步 Migration 夹具已经补齐；当前仍无守卫的主要缺口是并行执行尚未实现，因此没有 1-vs-N 线程安全对拍，以及黄金值仍可能被人为重置、只能依靠评审纪律。Authoring DSL 的 Parse / Resolve / Compile / Diagnostic 四面黄金锁定已经闭合；`CONTRIBUTING.md` 已指向该文件。

### 4.3 Demo 0.5：External Mechanism Vertical Slice（**已封存 2026-09-01**，持续回归）

**目标日期：2026-11-15 ｜ 实际封存：2026-09-01**

**封存结论**：经五轮审查（其中四轮由用户提出）后冻结。封存时的可观察结果：

| 项 | 值 |
| --- | --- |
| Package / Source / 实例 / Tick | 5 / 29 / 13 / 12 |
| `balance` / `report_count` | 1165.0 / 56 |
| `progress` / `goods_output` / `reports_sent` | 75.0 / 18.0 / 8 |
| `treasury.money`（实体侧） | 等于 `balance` |
| `treasury_seen`（跨机制读） | 1154.0（滞后一 Tick，快照语义） |
| Windows x64 测试 | Debug 26/26、Release 26/26 |
| Parse / Resolve / Compile 黄金 | 3912、3137、2582 字节 |
| 诊断码 / 端到端触发 | 100 / 12 |

**门禁清单**（全部在 `demo_0_5_vertical_slice_probe` 内，除非另注）：
双运行 Fingerprint / Save / Replay Checksum 一致；经济包替换；技术包删除；
缺包拒绝；非法 Package 角色拒绝；**Mechanism Package 硬编码 Content 实体拒绝**；
Source 篡改拒绝；跨 Package 读档拒绝；存档恢复字节稳定；
**连续运行 vs 存档续跑 Tick 20 逐字节对拍**；全部实例逐一取值一致（8 生产 + 4 研究）。

**封存时仍开放、且已知的**：运行期角色重绑定（`SetRole`，有意不加，见 C2）；
Worker Pool 与 1-vs-N 对拍（当前能力主线完成后的独立性能阶段）；`-Werror`（构建问题，非架构问题，已搁置）。

上面那些**引擎能力**已由 Kernel 工程验证夹具证明（§4.0），不再是本阶段的门禁。Demo 0.5 的门禁改为：**用真实玩法纵深压这些能力，看它们在哪里先断。**

范围 —— 经济 / 科研 / 生产纵向切片，落在 `Dillen-Game/`：

- 三个领域各自成 **Mechanism Package**，交互一律经 **Contract Package**（§1.2.2），彼此不得直接依赖；
- 真实 **Content Package**：国家、省份、建筑等具体 Definition 与初始数据，而非程序化夹具；
- 领域内机制数与实例数达到能暴露组合复杂度的规模，不再是"两个刻意最小化的机制"。

验收 —— 前四条沿用（换包、删包、非法 Package 拒绝、Checksum 一致），并新增两条**真正未知的问题**：

- **Capability fire-and-forget v1 是否够用**：正式切片已经证明 v1 足以表达“生产报告 → 经济拨款 → 科研解锁 → 生产增益”的定向单提供者反馈链。同 Tick 多发送者对同一接收实例做读改写的丢失更新问题**已于 2026-09-01 闭合**（见下方“多发送者汇聚”）。v1 ABI 本身继续冻结，返回值与关联 ID 仍留待 ABI v2 纯加法引入（§4.2）。
- **加载期成本**：正式切片为 5 个 Package / 29 个 Source Artifact，探针对 Parse → Resolve → Compile → Freeze 设置 30 秒硬门禁并输出实测微秒值；这建立了最小正式基线，但更大规模 Package 图仍需独立基准。

**正式验收结果（2026-08-31）**：`Dillen-Game/` 已成为唯一内容真相源；一个 Contract Package、三个 Mechanism Package 和一个 Content Package 共生成 13 个机制实例并运行 12 Tick。`demo_0_5_vertical_slice_probe` 已覆盖双运行 Fingerprint / Save / Replay Checksum 一致、经济包替换、技术包删除、非法 Package 角色拒绝、Package / Source Lock、Source 篡改拒绝、存档恢复和跨 Package 读档拒绝。旧 `tests/fixtures/dillen_demo_0_5` 已删除，避免正式内容与测试夹具分叉。

**第二轮审查修复（2026-09-01，由用户审查提出）**：

1. **多发送者汇聚不是原子累加（最严重）**。`AddFieldComputed` 在 VM 里先用 Dispatch 期快照算出绝对值，
   再发一条普通 `SetField`。八个生产站点同 Tick 向同一个预算实例报告时，八条命令读到同一个陈旧基数、
   算出同一个 `base + x`，最后一条覆盖前七条——**八份报告只落地一份**。修复方式是向
   `MechanismCommandOperation` **末尾追加** `MechanismAddFieldOperation { field, delta }`（内层 tag 7，
   纯加法，旧存档不含该 tag），命令只携带增量，由 Executor 对**已提交值**做读改写；VM 本地 `values[]`
   仍取绝对结果，好让同一程序后续指令看到。修复前 `report_count = 6 / balance = 240`，
   修复后 `48 / 996`——与用户手算一致。
2. **探针只验证第一个实例**。`FindField()` 固定读序号 0，八个生产站点和四个研究项目各只有一个被看过。
   新增 `UniformField()`：读**该 Definition 的全部实例**，同时断言实例数与逐实例取值一致。
3. **正式内容未全部参与 Gameplay**。`treasury.money` / `treasury.science` 只在 `.dentity` 里躺着。
   现已接入：预算机制 `create` 期把 `capital → treasury.money` 读作起始国库（25.0）；
   研究机制新增 `tick` 入口，把 `sponsor → treasury.science` 当作每 Tick 基础科研点。
   顺带证明了整数 Component 字段可直接汇入 decimal 机制字段。
4. **缺少连续运行对拍**。原有检查只做“存档 → 读档 → 再存档，字节相同”，这是**编解码**的性质，
   不是**模拟**的性质。新增 `CheckSaveResumeEquivalence()`：同一份内容跑两遍 Tick 1–20，一遍直通，
   一遍在 Tick 12 存档后**载入全新 Session** 再跑 13–20，要求 Tick 20 的存档逐字节相同。
   判别力已用注入验证：让 Capture 丢弃在途 Scheduled Event，旧检查**全程放行**，新门禁精确捕获，
   且两份存档**字节数完全相同**（5894），只有校验和不同——又一次说明字节数单独作断言没有意义。
5. 文档数字对齐：诊断码正文 96 → **97**（与表格一致）。

**C1：计算式 `set_component_field`（2026-09-01）**

上一条的 treasury 接线撞出来的：**机制能读实体 Component，但只能往回写字面量**。
解析器那行是 `scalarValue("value", output.operand)`，且只有 `SetComponentFieldConstant`
一个操作码。后果是国库可以被读成收入，但**永远不会被花掉**——读写单向，任何真实经济都会立刻卡住。

补法（纯加法，不动存档格式）：

- `AlgorithmInstructionKind` 与 `AlgorithmBytecodeOpcode` 各**末尾追加** `SetComponentFieldComputed`；
- `set_component_field` 接受 `left` / `op` / `right`，与 `value` 互斥，语法与 `set_field` 完全一致；
- **两条下降路径都要改**：`lowerTransact`（受控脚本）在 1434 起，声明式后端在 2347 起，是两份独立代码——
  这正是 2026-08-31 `-Wswitch` 那次的教训，不能只改一份；
- 实体可达性分析也要认新种类，否则 Package 能写进它没声明的 Entity；
- 复用已有的 `ComponentSetFieldCommand`，**World 命令与存档格式一个字节都不动**。

一个设计选择值得记：`AlgorithmBytecodeInstruction` 新增了 `componentFieldKind`。计算式写机制字段时，
VM 能从实例自己的值里读出目标类型；但 Component 在另一个对象上，VM 去找它还得在 Component 缺失时猜。
编译期为了分配 Slot 本来就解析过 layout，所以把 kind 记在指令里，VM 按事实量化而不是按观察。

**加法性机器证明**：操作码已编入但 fixture 未使用时，Compile 黄金**保持 2462 纹丝不动**；
fixture 加上新构造后才移到 2509。这是这条黄金存在的意义。

**判别力验证（两处，都注入过）**：

1. 正式内容里预算每 Tick 把 balance 写回 `treasury.money`。把这条写回删掉后，机制侧全部数字
   **依旧正确**（1165 / 56 / 75）——因为预算只在 create 期读一次国库——只有新加的
   `result.treasuryMoney == result.balance` 断言把它抓住。没有这条断言，整个 C1
   可以静默失效而 26 个测试全绿。
2. `dsl_read_operand_probe` 覆盖**整数目的地**（Demo 那条写的是 decimal，两条量化路径分开）：
   fixture 每 Tick 把 `progress * 3` 写进 outpost 的 `stock.ore`，四 Tick 后应为 12。
   把 VM 整数分支改成 `stored + 1` 后被精确捕获。
   写的是 outpost 而不是 capital：`home` 角色绑的是 capital，写它的 `ore`
   会反馈进下一 Tick 的 `output`，把一条写路径断言变成耦合断言。

**受控脚本后端也已断言**：`controlled_script_probe` 现在要求计算式 `set_component_field`
在 Script 阶段解析成 `Transact` 并带上正确的二元算子。上一轮 `-Wswitch`
的教训就是这条路径可以整个没接而声明式一切正常——平价只有被断言才是真的。

**顺带补齐的文档缺口**：`DILLEN_AUTHORING.md` 此前**从未记载读路径语法**——
`left` / `op` / `right`、四个根、`relation` 一跳、五个归约、六个二元算子、定点算术规则，
以及“`add_field` 计算形式提交增量而 `set_component_field` 提交绝对值”的理由，
全部是内容作者写 Package 时必须知道但文档里查不到的。已补成独立一节。

**C2：`mechanism_instance` 角色槽的写入路径（2026-09-01）**

此前记录说"写入侧三条路全封死"。实际去做才发现，**真正封死的只有一条**：

| 记录的封死路 | 实情 |
| --- | --- |
| `.ddefinition` 的 `roles` 只支持 Entity | **正确，且应当如此**。Definition 写就时实例不存在，Entity 的稳定 ID 却能由 `(entity_type, definition)` 直接导出 |
| `.dspawn` 根本没有 `roles` 键 | **这才是唯一的缺口**。`MechanismSpawnDefinition::initialRoles`、注册期 `RoleBindingsValid`、编译器下降、实例创建时的 `instance.roles = spawn->initialRoles` 全都已经在了，只差解析器读那一个键 |
| `MechanismCommandOperation` 没有 SetRole | **有意不加**，理由见下 |

补法：抽出共享的 `ParseRoleBindings`，Definition 与 Spawn 共用，只差一个
`allowMechanismInstance` 开关。Spawn 侧新增 `mechanism_instance = { mechanism definition ordinal }`
目标形式——`ordinal` 可省略默认 0——解析成 `StableMechanismInstanceId(StableMechanismDefinitionId(type, definition), ordinal)`，
`reference.type` 取 Mechanism Type 域的哈希，与 `ParseReferenceType` 对 `reference_type` 的解释一致。

**一处引擎行为必须改**：Schema 里 `reference_kind = mechanism_instance` 且 `minimum_count >= 1`
的角色槽，会让 **Definition 注册直接失败**——因为 Definition 层无法满足它。
把这一类槽的最小数量检查从 Definition 注册移到 Spawn 注册。**约束没有放松**：
Spawn 注册本来就把 Definition 与 Spawn 的绑定合并后再逐个校验 Schema 角色，
删掉 `.dspawn` 里的绑定后加载期立刻拒绝（注入验证过）。区别只是检查发生在
唯一能满足它的地方，而不是必然失败的地方。

**正式内容验证**：生产站点新增指向预算实例的 `treasury` 角色槽，
`invoke_capability` 从广播改为 `target_role = treasury` 定向，
并新增 `treasury_seen` 字段读取预算的 `balance`——走的是
`AlgorithmReadTerminal::MechanismField`，**这条读终端在角色槽可写之前内容根本无法触达**。
`treasury_seen = 1154` 而最终 `balance = 1165`，差一轮：生产 Tick 读的是派发期快照，
看到的是最后一 Tick 开始时的余额。这个滞后本身就是快照语义的证据。

**判别力，两处注入**：

1. 删掉 `.dspawn` 的绑定 → **加载期**被 Spawn 注册拒绝（`spawn_rejected`）。
2. 绑定存在但 `ordinal = 1`（该实例不存在）→ **运行期第一 Tick** 就 Fault，
   报 `read path target Mechanism field is missing`，不是静默读空。

**`SetRole` 运行期操作：有意不加，理由与 `cancel_event` 完全相同。**

运行期 Spawn 的序号来自 `nextOrdinalByDefinition` 这个递增计数器，
所以算法**无法知道自己刚刚创建的实例的 ID**。读路径产出的是用于算术的数值标量，
没有任何操作数来源能产出一个 Mechanism Instance 引用。因此一条 DSL 的 `set_role`
只能写字面 `(definition, ordinal)`——那与 `.dspawn` 的静态绑定表达力完全相同，只是发生得晚一点。

代价却不对称：`MechanismCommandOperation` 是**冻结的磁盘 tag**，追加了就永远不能删。
为一个只能用错的构造钉一个永久 tag，比留着可见的缺口更糟。
等 Capability ABI v2 的返回值或某种"引用型操作数来源"落地、能表达
"我刚生成的那个实例"之后再补——那时它才有正确用法。

**第三轮审查修复（2026-09-01，由用户审查提出）——Demo 0.5 封存前的最后一批**

**1. 隐藏的跨包耦合（最严重，且是我在 C1 里亲手引入的）**

C1 那条写回把 Content 实体名直接写进了 Mechanism Package 的算法：

```
# Dillen-Game/economy/demo_0_5/algorithms/budget.dalgorithm，错误写法
set_component_field = {
    owner_entity_type = dillen.demo05.country
    owner_definition  = dillen.demo05.alvara     # ← Content 的名字
    ...
}
```

而 `economy.dpackage` 只声明依赖 Contract 包。**这是一个包没有声明、也不可能声明的依赖**——
一个隐藏耦合，直接违背"不硬编码机制、实体等游戏要素"这条核心设计目标。
更糟的是：当时 26 个测试全绿，换包门禁也过了。**可替换 Package 这个承诺当时是假的**。

两半修复：

- **按角色寻址**。`set_component_field` 新增 `role = <槽名>` 形式，与
  `owner_entity_type` / `owner_definition` 互斥。实体在运行期从角色槽取，Content 负责绑定，
  机制只认识槽名。追加两个操作码 `SetComponentFieldByRoleConstant` /
  `SetComponentFieldByRoleComputed`（末尾追加，字面量形式一字未动，Compile 黄金保持 2509）。
  编译期校验该槽的 `reference_kind` 必须是 Entity——机制实例没有 Component，
  否则每次调用都会在运行期失败。运行期一个绑定目标发一条命令：槽可以合法地绑多个实体，
  只写第一个是静默的部分写入。
- **加载期边界检查**。新增 `dillen.authoring.package_entity_reference_violation`：
  **Mechanism Package 的算法不得点名具体 Entity Definition**——覆盖 `create_entity`、
  `set_component_field` 的 owner 形式、`add_relation` 的端点，声明式与受控脚本两条路径都查。
  规则的依据很直接：Mechanism 包只能依赖 Contract 包，而 Contract 包声明 Schema 不声明实体，
  所以出现在机制算法里的实体名**必然**是一个没有声明依赖的名字。

  判别力已验证：把硬编码改回去，检查在**摘要门禁之前**就报错并指名文件与构造。

  改成按角色寻址后 Demo 数字**完全不变**（1165 / 56 / 1154），说明两种寻址等价，
  换掉的只是耦合。

**2. 整数增量被限制到定点范围**

`MechanismAddFieldOperation` 对整数字段也走 `IntegerToInternal()`。内部标度是 10⁴，
该函数必须拒绝超过 ±9.2e14 的值否则放大后溢出——于是一个完整 int64 字段的可加范围
被压到约万分之一，合法的大整数加法被当作溢出拒绝。这是 C1 一并引入的回归。
改为**带检查的 int64 直接相加**：小数需要标度，整数从来不需要。

**3. 定点边界的未定义行为**

`FixedSubtract` 先 `-right` 再交给 `FixedAdd`；`FixedMultiply` / `FixedDivide`
先取绝对值再检查 `== kMin`。取负 `kMin` 没有可表示结果，是 UB——
**而且恰好发生在那个本该阻止溢出的检查的路上**。Sanitizer 会 trap，
优化器有权假定它不会发生并删掉检查。

`DivideRounded` 里还有第三处：舍入判定用 `-denominator`，而 `FixedDivide`
可以把 kMin 直接传进来。改用无符号幅值，并把 `2*|r| >= |d|` 重排成
`|r| >= |d| - |r|`（`|r| < |d|` 保证不会下溢），比较变成全域可用，
顺带去掉原先那段 `kMax/2` 特判。

减法改为直接检查而不是取负后转发。这里有个陷阱：**"凡 kMin 一律拒绝"会把正确答案变成错误**——
`kMin - kMin` 是 0，`-1 - kMax` 恰好是 kMin，两者都可表示。
我第一版极值测试就写错了这两条期望，是代码对、测试错。

新增 `CheckExtremes()` 极值向量：三个运算在 kMin / kMax 边界上的行为，
包括那两条"可表示、不得拒绝"的。

**4. 角色约束的 API 缺口**

- `MechanismInstanceStore::CreateFromDefinition()` 在产品代码里**一个调用点都没有**，
  只有测试用；但它是公开的，且不检查必填角色。Definition 注册故意允许机制实例角色为空
  （由 Spawn 注册兜底），于是这条路径能造出必填角色为空的活实例，
  之后每一次读路径与定向调用都会 Fault。补上同样的检查，新增
  `RoleBindingMissing` / `LayoutMissing` 两个结果。
- 重复角色名被 `emplace` 静默丢弃：作者在文件里看得见自己的绑定，引擎却从不应用它。
  改为报 `dillen.authoring.role_binding_duplicate`。

**5. 权威文档过期表述**：`FROZEN_CONTRACTS.md` 仍称机制角色不可绑定、仍写 18 个 tag；
备忘录 §3.19 仍称多发送者累加未实现，与 §4.3 自相矛盾。均已更正。
诊断码 97 → **100**（本轮新增 `component_owner_ambiguous`、
`package_entity_reference_violation`、`role_binding_duplicate`）。

**这一轮最值得记住的一条**：#1 不是能力缺失，是**我做 C1 时为了让内容跑起来而走的近路**，
而且它通过了当时全部 26 个测试。架构约束如果没有加载期检查，
就只是文档里的一句话——补上检查之前，"Mechanism Package 可替换"是无法验证的承诺。

**第四轮审查修复（2026-09-01）——上一轮修复自身的缺口**

上一轮修 #1 时新加的按角色寻址写入，自己带进来三个洞。这一轮全部是"修复的修复"。

**1. 多目标写入不消耗预算**

我写了注释说"一个绑定发一条命令，只写第一个是静默的部分写入"，却没为额外目标计费。
指令预算是**一次算法调用能提交多少工作的唯一上界**，一个角色绑一百个实体就能让
一条计费指令发出一百条命令。改为与 `cancel_events`、与聚合读路径同规则：
`bound.size() - 1`。

**2. Component 版本歧义**

`selection.components` 是 `set<pair<ComponentTypeId, version>>`——同一类型**可以**选入两个版本，
而实体各自声明 `schema_version`。Slot 按 (type, version) 的字段名排序分配，
所以两个版本下 slot 3 是两个不同字段。而按角色寻址的指令**带不了版本**：
它要写的实体到运行期才知道。在两个版本共存时，这条指令不是"未校验"，是**无解**。

裁定按项目对 Capability 提供者的既有立场：**拒绝歧义，而不是用一条谁也看不见的规则去消解它**。
编译期新增 `ComponentSchemaVersionAmbiguous`——一份组合 Ruleset 中每个 Component 类型只允许一个版本。
`AlgorithmBytecodeInstruction` 里也写清了为什么这对 (type, slot) 是充分的：
不是靠运气，是靠这条加载期规则。

这条规则**故意从严**。以后要放宽成"每实体版本"可以，反过来收紧则不行——
一旦有内容依赖了宽松语义就再也收不回来了。

**3. 回归门禁不足**

角色常量写入、多目标写入、预算耗尽三条运行期路径**一个测试都没有**；
新增的三个诊断码也只进了字符串清单，从未被真正触发过。

- `declarative_algorithm_vm_probe` 新增一组:角色槽绑三个实体，断言发出三条命令、
  三条各自指向正确实体、预算恰好消耗 3 个单位；预算给 2 时必须失败而不是少写几个实体；
  槽为空时必须 Fault 而不是空操作。两处注入验证过（去掉计费 → 消耗读到 1；
  只写 `bound.front()` → "did not reach every bound Entity"）。
- `authoring_diagnostic_contract_probe` 端到端触发从 9 条增至 **12** 条，
  新增 `component_owner_ambiguous`（两种写法：一个都不给 / 两个都给）
  与 `role_binding_duplicate`。探针原先只会用算法解析器，现按根关键字分派，
  否则 Spawn 用例会先在根关键字上失败、永远到不了被测诊断。
- `package_entity_reference_violation` 做成**常驻门禁** `RejectHardCodedContentEntity()`：
  复制 economy 包，把写回从按角色改回点名实体，要求加载被拒。
  这条不能只做一次性注入——它拦的那个违规是**手写进仓库并通过了当时全部测试的**。

  写这条门禁时踩了一个坑值得记：最初用 `text.find("role = capital")` 定位，
  结果命中的是 `create` 段里那条**读**路径，改写后产生的是语法错误而不是边界违规——
  一个伪装成失败的假通过。改为先定位 `set_component_field` 再向后找。

**4. 文档矛盾**：§3.19 第 20 条仍称多发送者写入会丢失更新，与 §4.3 冲突。已更正。

**这一轮的教训**：#1 到 #3 全部是上一轮修复引入的。一个为解耦而加的新寻址方式，
带来了新的预算面、新的版本解析面和新的运行期分支，而我只测了正式内容用到的那一条路径。
**修复本身也是新代码，也要按新代码来验。**

**第五轮审查修复（2026-09-01）——冻结前的最后一批**

**1. 角色写入未进入 Compile 黄金夹具**

编码器支持两个按角色寻址的操作码，夹具却只构造了具名 Entity 写入。
**这正是我自己在上面写下的那条规则**——"没有被构造过的备选项等于没有被冻结"——
而我在同一份文档里第三次犯了它（前两次是 `AddField` 的内层 tag 7、
`SetComponentFieldComputed`）。

夹具补上两条按角色写入（常量形式与计算形式），黄金从 2509 移到 **2582** 字节。
判别力已验证：把编码器里的 `targetRoleSlot` 故意写偏一位，
**字节数不变（2582）、校验和移动**——正是只有校验和能抓的那一类。

**这条规则显然靠记性是守不住的。**它已经写进 §4.2 命令编码黄金值条目，
但对 Compile 黄金同样成立：`EncodeInstruction` 每加一个 `case`，
`coverage.dalgorithm` 必须同时加一条构造它的指令。

**2. 补两个缺失的拒绝测试**（`runtime_catalog_probe`）

- `RejectsTwoComponentVersions()`：同一 Component 类型注册 v1 与 v2，
  v2 多一个**排序在 `amount` 之前**的字段——这才是真正移动 Slot 的原因，
  slot 0 在 v1 是 `amount`、在 v2 是 `added`。要求编译失败并给出
  `ComponentSchemaVersionAmbiguous`。
- `RejectsUnfilledRequiredRole()`：一个必填的 `mechanism_instance` 角色，
  Definition **必须仍被接受**（否则这类 Schema 就无法注册了），
  而 `CreateFromDefinition()` 必须返回 `RoleBindingMissing` 且不留下任何实例。

两条都用关掉对应检查的方式验证过判别力。

放置位置本身也是个教训：我最初把它们写进 `mechanism_instance_store_probe`，
而那个探针挂在 `DILLEN_BUILD_HOI3_COMPATIBILITY` 下，**标准 26 项里根本不跑**——
一个永远不会失败的测试。改放到无条件构建的 `runtime_catalog_probe`。

**同一轮暴露出的冻结方法论问题**：追加内层 tag 7 之后，26 个测试在**黄金值一字未动**的情况下全绿。
原因是 `CheckFrozenCommandEncoding` 从没构造过这个备选项——**没有被构造过的备选项等于没有被冻结**。
补上一条 `AddField` 命令后黄金从 516 字节移到 539 字节。这与 2026-08-31 由 GCC `-Wswitch` 暴露的
“黄金编码器少三个分支”是同一类错误，已在 §4.2 命令编码黄金值条目里写成硬性规则。

前置项（见 §4.2 冻结后的守卫缺口）：

- ~~**Authoring DSL 尚无黄金锁定**~~ —— **已闭合（2026-08-31）**。四面全部锁定，见 `FROZEN_CONTRACTS.md` 第 5 节：

  | 面 | 守卫 | 黄金值 |
  | --- | --- | --- |
  | Parse | `authoring_frontend_golden_probe` | 3912 字节 / `1278464547742860928` |
  | Resolve | 同上 | 3137 字节 / `15737711886577553487` |
  | Compile | `authoring_compile_golden_probe` | 2582 字节 / `5867319647378757321` |
  | Diagnostic | `authoring_diagnostic_contract_probe` | **111** 个码 + 18 个端到端触发 |

  诊断码当前是 **111** 个：在既有语法与管线诊断上新增 `invoke_capability` 载荷互斥校验，以及 Package 角色、依赖角色和严格显式角色诊断。

  **硬性规则（三次踩坑后写下）：`EncodeInstruction` 每加一个 `case`，
  `tests/fixtures/dillen_dsl_v1/algorithms/coverage.dalgorithm` 必须同时加一条构造它的指令。**
  命令编码黄金同理（见 §4.2 命令编码黄金值条目）。
  没有被构造过的备选项等于没有被冻结——`AddField` 内层 tag 7、
  `SetComponentFieldComputed`、按角色寻址的两个操作码，三次都是加了编码分支而没加夹具构造，
  三次都是全套测试在黄金一字未动的情况下全绿。**这条规则靠记性守不住，只能靠 review 时对照。**

  执行顺序为：先锁现有构造（1617 字节）→ 加读操作数与聚合、黄金值必须不动 → 用真实经济—科研—生产替换草稿 → 扩充 fixture 至完整覆盖后重取（2377 字节）→ 追加 `invoke_capability payload_from` 与 Package 角色契约后重取（2449 字节）→ 角色读路径按 `reference_type` 解析、并追加 `cancel_events` 后重取（2462 字节）→ 追加计算式 `set_component_field` 后重取（2509 字节）→ 追加按角色寻址的两个 Component 写入操作码后重取（2582 字节）。旧常量载荷编码保持不变，只有使用动态载荷的新构造追加读路径编码，因此仍遵守纯加法冻结规则。

  **这个漏洞是 GCC 的 `-Wswitch` 报出来的，不是自查发现的**：黄金编码器的 switch 少了三个 enum 分支，冻结面正好在新功能处开洞。同一批告警还暴露出 `IsValidAlgorithmInstruction` 漏校验两个新指令种类、以及**受控脚本后端整个没接读操作数**（`lowerTransact` 与脚本侧条件下降是独立于声明式的另一份代码）。MSVC /W4 三条都不报（C4061/C4062 默认关闭）。这是 Linux 阻塞门禁在本轮的实际产出。

### 4.4 地图与 Presentation 纵向切片（原 Demo 0.8，冻结候选）

**当前状态：基础能力已完成，产品 Demo 编号已撤销；耐久性门禁转入非阻塞维护清单，不再定义当前产品主线。**

**历史定位**：Demo 0.5 已证明外部机制能经过 Authoring、Compile、World、Runtime、Capability 和 Persistence 管线形成真实业务闭环。本切片验证这些核心能力能否承载一个可观察、可交互的地图与机制 UI：地图、通用 GUI、窗口与输入成为主要工作面；Persistence / Migration / Replay 继续作为地图基础正式冻结前的强制门禁。本节保留实施过程和审查记录，但不再定义后续产品路线。

#### 4.4.0 P0 表现层边界（已完成 2026-09-01）

在写任何窗口或渲染代码之前先把边界钉死，因为 §4.4.2 的状态边界目前**只是文字**：
`PackageRole::Presentation` 在枚举里，`PackageRoleAllows` 对它返回 `false`，
代码与守卫里没有任何东西约束方向。

**"表现层可被删除"现由三道互不重叠的机制保证**：

1. **模块方向**——新增 `src/presentation`，`architecture_guard_probe` 的允许边表里
   它可以到达 kernel / world / runtime，而**没有任何模块把 `presentation` 列进自己的
   `mayInclude`**。因此权威层任何一处 include 表现层头文件都会让该探针失败。
   注入验证过（`kernel/fixed_point.hpp` 加一行 include → 精确报出模块与行号）。

2. **构建分离**——`dillen::presentation` 被构建，但**刻意不链接进 `dillen::standalone`**。
   精确的性质是：**没有任何权威侧目标依赖表现层**。
   因此移除它是一次机械删除（目录 + `add_subdirectory` 那一行 + 测它的探针），
   而不是一次重构。**不是**「只删目录就仍能配置构建」——
   `add_subdirectory` 是无条件的，会先失败（2026-09-02 实测确认）。
   这个区别值得写清楚：强版本的说法会一直被相信，直到有人真去试。

3. **内容分离**——**Presentation Package 不进入确定性闭包**。
   依据是 Package Lock 的每一条目（id、版本、`content_digest`、载入序）
   都被哈希进 Ruleset Fingerprint，而存档校验的正是它。所以进了闭包的
   Presentation Package 会把**皮肤放进存档身份**——换皮肤则既有存档读不了，
   两个用不同皮肤的客户端会为同一份模拟算出不同指纹，直接违反 §4.4.2
   "纯 Presentation Package 更换不得使 Gameplay Save 失效"。

   规则：Ruleset 不得 require Presentation Package；任何包不得依赖它；
   Presentation Package 自己也不得声明依赖（在闭包外依赖永不解析，
   静默无效的声明比拒绝更糟）。它仍被解析、仍校验 manifest 与 `content_digest`，
   只是不进 Package Lock、不进 Source Lock、不进 Ruleset Fingerprint。
   新增诊断码 `dillen.authoring.presentation_package_not_authoritative`。

**`PresentationView`**：表现层持有 `WorldQuerySnapshotHandle`
（`shared_ptr<const WorldQuerySnapshot>`），到权威存储**没有任何非 const 通路**；
存储是写时复制，持有一份是引用计数而非世界拷贝。`Advance()` 拒绝空快照、
未发布快照，以及 publication 不前进的快照——表现层是唯一跨 Tick 持有快照的读者，
乱序换入会让观众看到一个"未发生"的世界。

**门禁**：`presentation_boundary_probe`（加入时 Windows x64 Debug / Release 为 **33/33**）
断言指纹对 Presentation Package 的存在与否完全无差别、Package Lock 条目数不变、
依赖它被拒、读句柄单调。指纹不变这条已注入验证：让 Presentation 源层重新走
Lock 成员资格检查，加载立刻失败。

**P0 刻意没有做的两件事，以及为什么**：

- **Presentation Package 现在什么都不能拥有**。地图几何、调色板、视图定义的格式
  都还不存在，在有真实地图数据可对照之前发明它们，会重犯本项目已裁定过两次的错误
  （按序列号的 `cancel_event`、`SetRole`）：只能用错的语法比可见的缺口更糟。
  第一个表现层构件类型与它所描述的地图内容一起落地（§4.4.3 第 1 步）。
- **独立 Presentation Lock / Presentation Fingerprint 尚未建立**。§4.4.2 要求它，
  但在 Presentation Package 还不拥有任何构件时，它锁的是空集。
  P0 先保证的是**排除**（不污染 Gameplay 身份），**独立身份**随第一个构件类型一起建立。

#### 4.4.0.1 P1a 省份栅格导入（已完成，2026-09-01）

**放置**：`src/adapter/province_raster_import.{hpp,cpp}`。它读位图与 CSV、推导邻接，
**不含任何 HOI3 语义**——同样的格式 Vic / EU4 也在用，所以它属于 §2.3 的
External Corpus Importer 层，而不是 HOI3 兼容树。后者在标准预设里不构建，
探针不会跑，这个坑刚在 Demo 0.5 封存前踩过一次。

**语料实测（`province_raster_import_probe`，Release 103 ms / Debug 9.2 s）**：

| 项 | 值 |
| --- | --- |
| 栅格 | 5616 × 2160，24bpp |
| 省份 | **14187**，id **1..14187 连续**，零重复 |
| 推导邻接 | **41693** 条，平均度 **5.88** |
| 孤立省份 | **0** |
| 表中声明但无 id 的颜色 | 57 个，栅格实际一个都没用 |
| 表中完全没有的颜色 | 26 个（白色 + 单像素噪点），占 **0.117%** 像素 |

**邻接必须推导，不能读**：`adjacencies.csv` 只有 **176 行**——那是海峡、运河一类的
**例外**，普通邻接由两省在栅格里共享边界隐含。完整邻居图只能靠扫描位图得到
（每像素与右邻、下邻比较；世界地图东西边界相接，所以还要跨日期变更线接一次）。

**一处我先前报错、必须更正的数字**：第一次分析时我说“14163 个省份、id 不连续、
24 个空洞”，**这是错的**。`definition.csv` 里有 24 行是**整行加引号**的：

```
"11576;0;15;236;pacific ocean;""x	"""
```

解析器（以及我当时那段 Python 分析，犯的是同一个错）看到首字段是 `"11576`
不是数字，整行丢弃。后果不是“少 24 个省份”这么轻——那 24 行是**海洋**，
栅格大量绘制它们，丢掉后它们变成“没人声明的颜色”，而被它们包围的太平洋环礁
（Yap、Ulithi、Wolelai、Truk、Majuro）就成了**没有任何邻居的孤立节点**，
即模拟图里不可达的点。修好之后孤立数从 9 变成 **0**，未知颜色像素占比从
0.632% 降到 0.117%——**“孤立数归零”本身就是这个修复正确的证据**。

判别力已注入验证：关掉引号行处理，三条断言同时报（省份数、未知颜色数、未知像素占比）。

**下一步（P1b）**：把这份导入结果变成批量 Dillen Content。一省一个 `.dentity`
不可行——14187 个文件会让 Source Lock 有 14187 条目、每条都进 Ruleset Fingerprint、
`content_digest` 要哈希 14187 个文件；41693 条邻接更不可能一条一个文件。
所以需要**批量内容形式**，这是 P1 真正会推翻“现有原语扛得住”的地方。

#### 4.4.0.2 P1b 批量内容与世界地图（已完成，2026-09-01）

**结论先说：现有原语扛得住。** 这是 P1 要证伪而没能证伪的假设。

| 项 | 结果 |
| --- | --- |
| 实体 / 关系 | **14187** / **41693** |
| 生成文件 | **7 个**（4 内容 + 1 Ruleset + 2 Manifest），2.1 MiB |
| **加载耗时** | **363 ms**，30 秒预算的 **1.2%** |
| 8 Tick 平均 | 0.0025 ms/tick（无机制世界的地板） |
| Package 摘要 | `d505fd0c58312abc57efff9405821960f0be9d19874f36594eae836106e456b3` |

**批量内容形式**：新增 `entity_table` / `relation_table` 两个构件类型
（`.dentitytable` / `.drelationtable`），一个文档产出多个内核对象。它们不引入任何新的
内核概念——产出的 `EntityDefinition` 与 `RelationDefinition` 和单条形式完全相同。
行写成 `row = { ... }` 而非扁平 item 流，是为了让坏行有自己的 span：
生成内容恰恰是错误最难读的内容。

**固化位置**：`Dillen-Game/map/contracts` 与 `Dillen-Game/map/world` 两个权威包；地图规模机制位于 `Dillen-Game/production/map_world`，纯表现资源位于 `Dillen-Game/presentation/map_world`。
运行期加载它时**没有导入器、没有位图、没有 CSV**——这正是生成 Dillen Content
而不是直接读语料的意义。

**过程中撞出的四个真实问题**：

1. **Content 包不能拥有 Schema**。最初把 Component / Relation Schema 和实体表放进
   同一个包，加载期直接拒绝。**分层检查是对的**：schema 是契约，一个把 schema 塞进
   自己 Content 的生成世界，任何不想要这张地图的机制包都用不了它。改为两个包，
   Content 依赖 Contract。
2. **`PackageContentSource::bytes` 是 `string_view`**。把 move 走的临时字符串存进去——
   编译通过、文件写对、然后在算摘要时读已释放内存，**段错误**。现在由 `PackageWriter`
   持有文本，并在 `reserve` 用满时直接失败：一次重分配会让之前发出的所有 view 悬垂。
3. **Ruleset 必须逐条列出实体与关系定义**——14187 + 41693 = **55880 个 requirement 块**，
   那只是把庞大文件从内容挪到 Ruleset。闭包裁剪是这条规则存在的理由，而且是对的默认值，
   所以没有取消它，而是加了**显式的整体选择**：`required_entity_definitions = { all = yes }`。
   opt-in、一行，且**受 Package Lock 约束**——“全部”指已锁定的包声明的全部，不是磁盘上的全部。
4. **生成内容的行尾无人保护**。`.dentitytable` / `.drelationtable` 已加入 `.gitattributes`
   的 `eol=lf`（现共 13 个授权扩展名全部钉住）。生成内容**没人逐行审阅**，
   CRLF 检出在 Package 摘要失败之前完全不可见。

**两个探针，职责分离**：

- `world_map_content_probe`：重新导入语料、重新生成到临时目录，与固化内容**逐字节比对**。
  固化的生成内容有一个手写内容没有的失效模式——它会和它的来源漂移，而没人会去读两兆字节的表格。
  所以检查的不是“能不能加载”，而是“今天是不是仍然等于语料所产”。
  `DILLEN_REGENERATE_WORLD_MAP=1` 是唯一受支持的修改方式。
- `world_map_scale_probe`：只加载固化内容（**不链接 adapter**），测加载与 Tick 预算。
  它会心安理得地测量一个已经漂移的世界——测量是它唯一声称做的事。

**判别力，三处注入**：实体表少发最后一行 → 引用缺失省份的边界关系被注册表精确拒绝并指名；
手改固化内容一个字节 → 比对捕获；把一个文件改成 CRLF → 靠**字节数差**（647 vs 635）显形，
正是那条 `.gitattributes` 存在的理由。

**下一步 P2**：表现读模型——省份索引 → 打包属性表，调色板。
P1a 已经证明全扫在这个规模上完全可接受（约 2–3 ms/次发布），增量是优化不是前提。

#### 4.4.0.3 P2 表现读模型（已完成，2026-09-02）

`src/presentation/province_projection.{hpp,cpp}`。把一次发布的快照变成一张
**按稠密省份索引寻址的扁平属性表**：第 i 行是索引为 i 的省份，
而 ID 栅格存的正是同一个索引，所以着色器的查表是一次 texel fetch，与省份数无关。

**实测（`province_projection_probe`，真实世界地图）**：

| 项 | 值 |
| --- | --- |
| 规模 | 14187 省 × 1 列 |
| **全扫耗时** | **2.238 ms / 次发布** |
| 缺失行 | 0 |

**这个数字兑现了 P1a 的预测**（2–3 ms），所以此前记为「读模型需要逐实体变更表」的那条
阻塞项**确认撤回**：按每秒 5 次发布算，全扫占不到一个核心的 1.5%，
增量更新是优化而不是前提，`WorldQueryStamp` 不必先改。

**三条被门禁钉住的性质**：

1. **纯函数**——同一份快照刷新两次字节相同；两个独立投影读同一份快照结果相同。
   一个会漂移的投影等于把表现状态混进了本应只是派生结果的画面。
2. **说的是世界说的话**——逐行断言第 i 行确实携带第 i 个省份的 `source_id`，
   对照内容而不是由投影自己假设：**整体偏移一行的投影仍然是一张尺寸正确的表**。
   注入验证过（`row -= columnCount` → 断言精确报出行不对位）。
3. **不写**——投影前后世界的 revision、实体数、组件数完全不变。
   `PresentationView` 只给 const 通路，所以这条除非有人强转否则不可能失败——
   正因如此才值得断言：边界是结构性的，就应当被看见成立。

**两个设计决定**：

- **第 0 行保留且恒为零**。栅格在地图之外画 0，所以渲染器可以直接用读到的值索引这张表，
  不需要分支判断这是不是一个省份。
- **全整数，小数按定点内部标度（10⁴）承载，不用 double**。
  下游要浮点时自己除；但从权威值到纹理上传之间，没有任何一步能在另一台机器上舍入出不同结果。
  字符串、引用、列表一律拒绝而不是静默投影成 0——需要它们的地图模式需要一种这张表还没有的列类型。

**命名约定作为数据传入**（`entity_type` 与 `name_prefix` 是 spec 字段，不是常量）。
表现层不假设任何特定世界；将来第一个 Presentation 构件类型落地后由它携带。

**下一步 P3**：渲染器——经纬网格 + 顶点着色器形变（球↔平面连续）+ ID 纹理 + 调色板 +
GPU 拾取 + 地图空间相机。SDL + OpenGL 3.3 core，等距圆柱投影。

#### 4.4.0.4 P3 表现层构件、视图数学与渲染器（已完成，2026-09-02）

##### P3a 第一个 Presentation 构件类型

`presentation_asset`（`assets/**/*.dasset`），**刻意不是「地图」**。Kernel 只知道
「表现层包声明具名资产，每个有 kind、载荷文件和摘要」；它不知道栅格、字体、布局是什么。
`kind` 与 `properties` 由表现层解释。这样将来加字体、布局、调色板都不必碰 Kernel。

**声明与载荷分离，是被数据逼出来的**：索引栅格 5616×2160 是 24 MB 二进制，没有合理的文本形式。
载荷**不被文件目录认领**（`Unclassified` 会被静默跳过），所以管线绝不会拿它当文本解析，
它也**不进任何包的 `content_digest`**。`asset_digest` 是唯一把声明和字节绑在一起的东西，
因此加载器**先验摘要再解码**。

**RLE**：平均游程 27.7 像素，24.3 MB → **1.71 MB**（13.8×）。按扁平数组而非逐扫描线取游程，
横跨行尾的海洋只算一个游程。

**独立 Presentation Fingerprint**（§4.4.2 要求的）：按资产规范名排序哈希，与 Ruleset
Fingerprint 完全分离。P0 时说「它锁的是空集」所以推迟，现在有内容了。
资产路径相对**声明它的源文件所在目录**解析（不是包根）——这样禁止 `..` 才有意义，
资产也必须随声明一起搬迁。

**门禁**（`map_index_raster_probe`）：P0 的边界在**真有内容之后**重新验证一遍
（加载表现层包不动 Ruleset Fingerprint、不进 Package Lock）；翻转载荷中间一个字节
（正落在游程计数里，否则会解码成功并悄悄产生一个略有不同的世界）必须在解码前被拒，
探针随后恢复文件并**验证恢复成功**；解码栅格与重新导入的语料**逐像素相同**——
RLE 往返加摘要只能证明载荷完整，证明不了它当初就是对的。

##### P3b 视图数学

三个空间彻底分开：**模拟空间**（图，无坐标）、**地图空间**（固定的 (u,v) 域）、
**视图空间**（由单个 `b` 参数化的嵌入）。球与平面是**同一个地图空间的两个嵌入**。

| 门禁 | 实测 |
| --- | --- |
| `b=1` 每点到球心距离 | 误差 **6.7e-16** |
| `b=1` 日期变更线闭合 | 间隙 < 1e-12 |
| `b=0` 等于平面且尺寸不变 | 误差 **1.1e-16** |
| 2000 步扫描，形状最大跳变 | **0.0028**（地图宽 2π，即 0.045%） |
| 2000 步扫描，相机最大跳变 | **0.000377** |

**弧长守恒自动定出纬度跨度** = ±π·H/W = **±69.2°**。这正是参考语料真实覆盖的范围，
所以 `b=1` 时球面**两极是开口的**——这是对的，那里没有数据，闭合它等于凭空发明。

**条件数**：`R = W/2πb` 发散而 `sin λ → 0`，是 0×∞ 型。全部改写成 `sinc` 与
`2sin²(x/2)` 形式，平面极限从同一个表达式里直接落出来，不需要分支。
注入朴素 `sin(x)/x` 后门禁在 `b ≤ 1e-5` 处精确报出非有限值。

**这一轮我先写了一个假测试，值得记下来**。「注视点恒在屏幕中心」这条：
把相机的 `normal` 故意改成从两个 `b` 缝合，**测试照样通过**。
原因是 `eye = target + normal·d` 而基向量又由**同一个** `normal` 构造，
所以注视点在轴上是**构造上恒真**的，与 `normal` 用哪个 `b` 算无关——它只能抓到矩阵转置。
真正的性质是**相机自身在 `b` 上连续**；改成扫描视图矩阵每个元素的逐步变化后，
同一注入立刻报出 `the camera jumps by 0.672 at b = 0.500`，是正常值的 1800 倍。
那条恒真的断言保留了，但**加了标注说明它只能抓转置**——
一个不可能失败的测试被当成能失败的，比没有测试更糟。

##### P3d SDL 与 GL 后端

依据 §1.6「新平台后端需要统一抽象」，渲染器按 Kernel 扩展判据成立，
故采用 **vendored 源码入库**：SDL `release-3.4.14` 固定 tag，落在
`Project-Dillen/third_party/`（**不在 `src/` 下**——架构守卫会解析那里每个 include）。
裁掉 `test/ examples/ Xcode/ VisualC*/ android-project/ docs/`，54 MB → **34 MB**；
这些都由 CMake 选项守卫，不留悬空引用。音频、手柄、传感器、摄像头、触觉、
SDL 自身渲染器全部关闭，只留 video + OpenGL。静态库 14 秒 / 6.4 MB。

`.gitattributes` 增加 `Project-Dillen/third_party/** -text`：顶部的 `* text=auto`
会去规范化 SDL 的行尾，那不是我们该决定的事，上游发布的校验和是按原字节算的。

**两个设计决定**：

- **顶点位置在 CPU 上算，GLSL 里不重写形变**。本项目已栽过两次「同一套语义两份实现」
  （声明式 vs 受控脚本）。着色器里再写一遍形变，被测过的数学就和实际画出来的分家了。
  131841 顶点 1.6 MB，只在 `b` 变化时重传。
- **GL 函数指针自己声明，不引第二个依赖**。生成式加载器要为约三十个入口再 vendored 一份；
  手写八十行没有构建故事，而且把真正依赖的 GL 表面集中在一处可读。

**拾取用 ID 缓冲**：颜色与省份索引由**同一次片元着色**写进两个附件，
所以拾取读到的就是观众看到的那个省份——在任意曲率下都成立，
而解析求逆在任意 `b` 下不可行。

**`map_renderer_smoke` 是唯一不进标准套件的目标**，且理由不是偷懒：它需要 GPU 与显示器，
Linux 四格两者都没有。它本可以证明的一切都已经被无头证明过
（`map_view_probe`、`map_index_raster_probe`、`province_projection_probe`）。
实测：5616×2160 索引纹理、14187 项调色板、三个曲率各一帧、三次拾取全部命中真实省份。

##### 一处必须更正的措辞

P0 时我在 `src/CMakeLists.txt` 与备忘录里写了「删掉 `src/presentation` 目录，
一切照常通过」。**实测这句话是错的**——`add_subdirectory(presentation)` 是无条件的，
配置会先失败。真正成立且被强制的性质是：**没有任何权威侧目标依赖表现层**
（`dillen::standalone` 不链接它，架构守卫禁止任何模块 include 它的头），
所以移除它是一次**机械删除**（目录 + 那行 `add_subdirectory` + 测它的探针），不是重构。
两处措辞均已改正。

#### 4.4.0.5 P4/P5 机制、命令闭环与 UI 绑定（已完成，2026-09-02）

##### P4-0 地图接上机制

地图必须**接机制**才谈得上验证承载力——只画出来不算。新增 Mechanism 包
`dillen.map.production_site`（模板 + 算法）与 Definition `dillen.map.site`，
Content 包用 `spawn_table` 一次性生成 **14187 个实例**，每个通过 `province` 角色
绑到一个地区实体上。

**角色寻址而不是名字拼接**：Definition 只声明它要一个 `province` 角色，
不知道地区实体叫什么。这是 Demo 0.5 那次跨包耦合（经济机制包里直接写死
`dillen.demo05.alvara`）改出来的规矩，`package_entity_reference_violation`
在加载期强制它。

##### P4a 从地图意图到权威命令

`MapCommandTranslator`：`MapIntent{Kind, provinceIndex, value}` → `MechanismCommand`。

- 发出的是 **`AddField`（增量）而不是绝对值**。表现层看到的是某个快照，
  权威世界在它读完之后还会继续 Tick;写绝对值等于把一次读当成一次锁。
- `Resolve()` 建立 省份 → 实例 的映射时**读角色绑定**，不做算术。
  实例 id 和省份序号看起来是平行的，但那是 Spawn 顺序的巧合，不是契约。

**门禁**（`map_command_probe`）：14187 个省份全部解析出实例;5 个意图 × 6 Tick,
与手工构造同一批命令的路径逐字节对拍，**4073946 字节存档完全相同**。
（这条断言最初是假的——手工路径也调了 `Translate()`,等于自己和自己比。
为此把 `InstanceFor()` 暴露出来,手工路径才真的独立。）

##### P4b 客户端状态不进世界

`ClientState{selected, hovered, camera, viewport}` + `Digest()`。
**门禁**（`client_state_probe`）：三个视图者各自选中不同省份、相机停在不同曲率,
`Digest()` 三者互不相同;跑完 3 意图 × 8 Tick 之后,**三条存档 4073946 字节完全一致**。
「选中」「悬停」「相机」是**观众的属性,不是世界的属性**——这条不靠约定,靠对拍。

##### P4c 机制面板

`MechanismPanel{Bind, Read}`：省份序号 → 一组带标签的值。刻意**与绘制分离**,
所以数据这一半是无头可测的,窗口只负责显示。

`Read()` 走的是 `translator.InstanceFor()`——**和命令路径同一次查找**。
这不是省事：两者一旦对不上,玩家读的是一个省的数字、下的是另一个省的命令。
整数按原值,小数按定点**内部标度**（10⁴）,与 `ProvinceProjection` 一致,
中间不做任何会因机器而异的舍入。字段名读不到时**在 Bind 期拒绝**,不显示空白。

**闭环实测**（`map_renderer_smoke`,窗口侧）：从 ID 缓冲拾取 → 读面板 →
发一个 `AdjustLevel` 意图 → 入队 → `RunTick` → 重读面板 → 刷新调色板重绘。
拾到省份 9839,`level 1 → 5`,`output 0 → 491950000`（内部标度,
即 9839 × 5 × 10⁴——`output` 是算法在随后那 Tick 从新 `level` 重算出来的,
只回显命令的面板会 `level` 对而这里错）。
**注入验证**：把 `Read()` 改成读 `provinceIndex + 1` 的实例,
无头探针报 3 处失败、窗口探针报 2 处失败,并打印出邻省的 `98400000`。

##### P5a UI 绑定作为表现层资产

新增资产 kind `ui_binding`,带 **`requires` 块**：
`PresentationAssetRequirement` 有三种类型（MechanismField / ComponentField / Capability）,
在**加载期**对着 Ruleset 解析。面板要读的字段名同时写在这里,
所以 Ruleset 不再提供某个字段时,世界**加载不起来**,而不是运行到一半显示空白。

**门禁**（`presentation_binding_probe`）：2 个资产、3 条有类型的需求,
一条解析不到的需求在加载期被拒。
（这条探针有两次因为错误的原因通过：先是改了声明没重封 `asset_digest`;
再是重算摘要时**把载荷也算进去了**——载荷是 `Unclassified`,根本不参与摘要。）

##### P5b 控件树与布局

**控件树写在 DSL 里,但 Kernel 一个 UI 词汇都没学到。**

这是本轮唯一的架构抉择。三条路：给 Kernel 加控件类型（把 UI 词汇塞进 Kernel,
正是 P3a 明确避开的）;把布局做成二进制载荷（丢掉 DSL 的诊断）;
或者让 presentation 依赖 `parser` 自己再解析一遍（多一条模块边,而且 `properties`
早就证明了不需要）。

选的是第四条：把 `properties`（**已经是**无类型平表）推广成 **`content`——无类型的树**。
Kernel 新学到的只有「嵌套」和「兄弟顺序」,没有 `panel`、`button`、`label`。
`presentation::ControlTree` 是唯一知道这些键什么意思的地方。
代价是解析器不能对 `content` 做任何白名单校验——所以**未知键必须由 ControlTree 拒绝**,
拼错的 `labell` 在一个干净加载的包里凭空消失,是这一层最该防的失败模式。

**`requires` 从装饰变成承重**：每个 `value` 控件绑定的字段名**必须**出现在同一资产的
`requires` 块里,否则 `ControlTree::Load` 拒绝。在此之前那个块是一张没人查的清单。
`MechanismPanel` 也改用 `tree.BoundFields()` 绑定,布局加一行不会绑出一个空白。

**布局全整数**。`fill` 按权重分,**余数归最后一个 fill 子控件**。
三个子控件分 100 像素得 33/33/34,精确铺满,没有一个像素属于两个控件或不属于任何控件。
浮点矩形在两处舍入不同,就是「点击落在看着不在的控件上」的来源。

**按钮只产生 `MapIntent`,不产生 Command**——`ControlTree` 根本够不着世界。

**门禁**（`control_tree_probe`,全程无头）：包里 8 个控件;
四个视口尺寸（含奇数宽,偶数宽会让余数规则测不出来）下容纳性与不重叠;
间隙里的像素不归任何按钮;按钮意图经同一个 translator → RunTick → 面板读数真的动了;
小数固定四位。**四处注入全部命中**：去掉余数规则（`201x300`/`201x301` 两处报错）、
拾取不下钻、不查 `requires`、静默忽略未知键。

**新增诊断码** `dillen.authoring.presentation_asset_content_invalid`,
冻结清单 114 → **115**,端到端触发 21 → **22**。

##### P5c 字体、文字与选择高光

**第二个 vendored 依赖:FreeType `VER-2-13-3`**,裁掉 `docs/ devel/ tests/`、
非本项目平台的 `builds/*`、以及不在 `BASE_SRCS` 里的 `src/tools|gxvalid|otvalid`,
13 MB → **8.0 MB**,静态库 1.9 MB。zlib / bzip2 / PNG / Brotli / HarfBuzz 全关。

**FreeType 不在 `DILLEN_BUILD_MAP_RENDERER` 之后,SDL 在**——这个分界是本轮的关键决定。
SDL 是**平台后端**,要窗口和显示服务器;FreeType 只把字节变成覆盖率位图,**不碰显示**。
所以文字层可以进标准套件,而一条只在有 GPU 的机器上跑的门禁,Linux 四格永远跑不到。

**字体是 Presentation 资产(带载荷 + 摘要),图集不是。**
图集只在一个像素尺寸下光栅化,烘焙它等于把界面钉死在一个尺寸和一块屏幕上——
而「尺寸是运行期决定」正是选 FreeType 而不是位图字模的全部理由。
所以包里发的是 TTF(Roboto Mono,OFL 1.1,183 KB,**等宽**,正好配定点固定四位小数),
`presentation::TextAtlas` 从它光栅化。**摘要在 FreeType 看到第一个字节之前验**——
把改动过的字体喂给字体解析器,正是没人想让它见到的输入。

**能无头测的全部无头测**:`TextAtlas`(度量、笔位、按整字裁剪)与
`BuildPanelOverlay`(哪些矩形、什么顺序、什么颜色)都是 CPU 上的算术。
窗口只被问一个问题:overlay 有没有落在布局说的像素上。

**门禁**(`text_atlas_probe`,无头):95 个字形 / 14px / 740×18 图集;
范围内只有空格无墨;`W` 的度量框里确实有墨(只写度量不 blit 会全过);
所有字形共享等宽 advance;`Measure` = advance 之和;
逐字符验证 `quad.x == rect.x + i*advance + bearingX`
(**比较相邻 quad 的差是不行的**——那等于 advance 加两个 bearing 之差,很多错误布局都满足);
超宽字符串按整字裁剪;面板每一行的真实字符串都放得下它的控件。
overlay:三块实心面(只有根 panel 画底,嵌套 panel 不画,否则 alpha 会越嵌越暗);
实心在文字之前(后端可以按顺序画,不排序不用深度);
悬停**只改一个 quad 的颜色**;没有字体时**面还在,只是没有字**。

**注入命中**:不 blit、摘要不先验、不裁剪、丢掉 bearing;
overlay 的每个 panel 都画底、忽略悬停、没字体就清空。

**选择高光由写 `oIndex` 的同一次片元着色决定**,所以亮起来的省份和拾取返回的省份
构造上不可能不一致。窗口侧读回像素验证:选中后中心像素颜色变了,
**而另一个省份的像素没变**(只断言选中像素会被「整张图重绘」蒙混过去),
且高光没有改变拾取结果。注入「从不高光」和「所有省份都高光」均被抓到。

**两处自己踩的坑,都值得记**:

1. `Present()`(SwapWindow)之后后缓冲内容**未定义**,我却在那之后才采样,
   探针报「39 个 quad 落位 0 个」。读回必须在 present 之前。
   `ColourAt` 还必须显式 `ReadBuffer(GL_BACK)`——`PickAt` 会把读缓冲留在 id 附件上,
   那个名字在默认帧缓冲上根本不合法。
2. 第三处注入(去掉 `GL_UNPACK_ALIGNMENT = 1`)**没有被抓到**。
   738 宽的图集在 4 字节对齐下会逐行错位,但采样字形中心仍会碰到墨。
   与其留一个测不出来的守卫,不如**把失败模式消掉**:图集宽度补齐到 4 的倍数
   (740),这条是无头可断言的。`PixelStorei` 保留,但它不再是唯一挡在那里的东西。

##### P6 可交互的地图查看器,以及它暴露出来的一个真 bug

**`dillen_map_viewer`:一个真正能操作的窗口。**
放在 `Project-Dillen/apps/`,**不在 `src/` 下**——架构守卫只走 `src/` 并把每个引号
include 解析到所属模块,而一个把四个库拼起来的可执行文件不该是其中一个模块;
没有任何东西链接它。它也**不 include SDL**:输入走 `MapRenderer::PollInput`,
平台后端仍然是一个可以整目录删掉的东西。

###### 地图是上下颠倒的——本轮最重要的一条

用语料自己的地标实测:导入后 Tromsø / Murmansk / Reykjavík 落在 y≈2030–2076
(共 2160 行,即**底部**),Ushuaia / Punta Arenas / Cape Town 落在 y≈68–243(**顶部**)。

BMP 头是正高度(按规范自下而上),**解码本身没错**;是这份语料的图像相对地理是上下镜像的。
所以新增的 `northAtImageBottom` 是关于**图像**的陈述,与 BMP **行序**这个存储细节
分开写——合成一个标志会让另一种约定的语料无法表达。

修在 **importer 而不是 renderer**:逻辑地理空间必须是对的,渲染器只被允许弯曲它。
重新生成后栅格摘要 `774c3c38…` → `71fb3d7b…`,而**世界内容摘要 `f62e4e4c…` 一个字没变**。

**这条为什么此前一个门禁都没抓到,值得单独记下来**:
数量、邻接、度分布、孤岛数、所有摘要,在垂直镜像下**全部不变**。
一个上下颠倒的世界能干净地导入、正确地 Tick、存档重载逐字节一致——**就是错的**。
它是被人**看着窗口**看出来的。这是本项目第一条「只有渲染出来才可能被发现」的缺陷,
也说明为什么 §4.4 要求做窗口而不只是做无头管线。

补的门禁在 `province_raster_import_probe`:六个地标(北三南三,用语料自己的 id),
断言北方三个在栅格北三分之一、南方三个在南三分之一、且南在北之下。
注入「不做镜像」后报 **7 处失败**。

###### zoom 与曲率

`zoom` 只有一处状态:`MapCamera::distance`,**沿局部外法线**度量
(`BuildMapViewMatrix` 里 `eye = target + normal · distance`),
所以它是**离表面**的高度而不是离球心。它属于 `ClientState`,不进存档。
取值范围 `[0.2, 2.0]` 与滚轮的乘性步进是**应用**的策略;
FOV 45° 与近远平面是**后端**的投影矩阵。三者分层,互不知道对方。

**滚轮同时驱动缩放和曲率**:拉远成球、拉近摊平。映射按**距离的对数**线性——
滚轮本身是乘性的(每格 ×0.9),线性映射会把几乎全部曲率变化挤进最后几格。

```
bend = clamp( ln(d / 0.2) / ln(2.0 / 0.2), 0, 1 )
```

端点精确落在两个极限。手动控制(`1..9 / 0` 与 `[` `]`)保留:
这套几何的两个极限值得能停在那里看,而耦合模式不会正好停在端点;
滚一下滚轮自动收回耦合。

**这不只是操作手感。** §4.4.1 要求「球体与平面是同一套地图几何的两个极限状态」——
把它们绑在同一个滚轮上,是让人**摸得到**这句话,而不是被告知。

###### 其余交互

中键(或右键)拖动转动地图,步长随相机距离缩放,拉近后地面不会从指针底下撕走;
相对位移从事件累加而不是每帧差分光标——差分会漏掉快速拖动的绝大部分。
左键保持「选中」。左下角有一行**应用自己画的**状态读数(曲率 / 缩放 / Tick / 选中省份)——
调试读数不该变成 Package 内容的一部分。

`SDL_GL_SetSwapInterval(options.visible ? 1 : 0)`:可见窗口垂直同步,
隐藏的冒烟测试不等。

#### 4.4.1 内容范围

1. **小型原创地图**：使用有限数量的地区和政治实体，不导入 HOI3 Corpus。
   **（已裁定 2026-09-01）**：改为**直接构建世界地图，不做小型地图验证**。
   地图语料（`Dillen-Game/map/source`，HOI3 原版位图与数据）已入库，
   实测 14187 个省份 / 41693 条邻接，见 §4.4.0.1。原文关于"有限数量"的表述作废。
   **地图渲染要求**：逻辑地理空间保持不变，只在 Renderer 中连续改变渲染曲率——
   球体与平面是同一套地图几何的两个极限状态。投影按**等距圆柱**处理，
   图形栈为 **SDL + OpenGL**。
   （以下为裁定前的分析，保留作为依据）本条的"有限数量"与"验证核心承载能力"存在张力。
   `scale_probe` 的合成负载在 N=16000 时为 30.1 ms/tick（Release，成本线性，
   快照发布约占 16%），但那是**同质**负载——每实例一条 `add_field`。
   地图是异质实体 + 邻接关系 + 多组件，加载期 30 秒硬门禁与编译预算从未见过这种输入。
   若地图只有几十个地区，可玩性验证成立而承载能力验证不成立。
   建议把二者拆开：可玩切片用小型原创地图；承载能力另用一个**纯规模夹具**
   （无渲染、四位数地区）压加载期与 Tick 预算。HOI3 位图数据可用于后者。地区是普通 Entity，邻接是 Relation，归属、人口、资源和机制状态是 Component；Kernel 不新增 `Province`、`Country` 等专用类型。
2. **首个 Presentation Package**：外部定义窗口、控件树、文本、图片、按钮、列表、进度条、Tooltip、Map Widget、数据绑定、动作绑定、贴图、字体和本地化。
3. **Standalone 窗口后端**：复用 `StandaloneSession`、Query Snapshot、Command 与 Fact Stream；窗口、渲染、输入和资源上传属于 Platform / Presentation Backend，不持有第二套世界状态。
4. **机制 UI**：继续使用 Demo 0.5 已封存的经济—科研—生产机制，通过公共 Query / Command / Capability Contract 显示状态并提交玩家操作；不为经济、科研或生产编写专用 Host / Kernel C++ 分支。
   **（偏差记录 2026-09-02）**：实际接的是**新写的地图机制包**
   `dillen.map.production_site`，不是 Demo 0.5 的经济机制。理由是 Demo 0.5 的机制绑在
   `dillen.demo05` 的实体上，把它搬到 14187 个地区上等于重做一遍内容，
   而本条真正要验证的是「不为业务写专用 C++ 分支」——这一点由新机制包同样成立，
   且它是**纯 DSL 内容**，引擎侧一行没加。见 §4.4.0.5 P4-0。
5. **地图交互**：支持地图显示、区域拾取、稳定 Entity ID 返回、选择高光和基础信息面板。颜色和图标由 Presentation Binding 从 Query Snapshot 计算，Map Widget 不理解“国家”“经济”或“所有权”的业务含义。
   **（已达成 2026-09-02）**：显示、拾取、稳定 ID、信息面板（§4.4.0.5 P4c）与
   **选择高光**（§4.4.0.5 P5c）全部达成。高光由写 `oIndex` 的同一次片元着色决定，
   所以亮起来的省份与拾取返回的省份构造上一致。
6. **最小输入闭环**：玩家至少能够选择地区、查看机制数据、提交一类经济/科研/生产 Command，并在后续 Tick 观察权威状态变化。
   **（已达成 2026-09-02）**：`map_renderer_smoke` 跑通 拾取 → 读面板 → 发意图 →
   `RunTick` → 重读面板 → 重绘 的完整闭环，见 §4.4.0.5 P4c。
   数据侧（面板读数、命令翻译、客户端状态隔离）全部另有**无头**门禁，不依赖 GPU。

#### 4.4.2 身份与状态边界

- 影响模拟的地图拓扑、Entity / Component / Relation Definition、Spawn 和初始状态进入 Package / Source Lock 与 Ruleset Fingerprint；
- 布局、贴图、字体、音频、纯表现地图几何、相机和高光进入独立 Presentation Lock / Presentation Fingerprint；
- 纯 Presentation Package 更换不得使 Gameplay Save 失效；Presentation Binding 必须在加载期验证其引用的 Query / Command / Capability Contract；
- UI 选择、悬停、窗口位置和相机属于非权威 Client State，不进入 Gameplay Save 或 Replay Checksum；读档后由 GUI 从 Query Snapshot 重建；
- GUI 只能读取不可变 Query Snapshot，只能通过经过验证的 Command / Capability 修改世界，不能直接获得可写 Store、Mechanism Instance 或 Component 引用。

#### 4.4.3 开发顺序

1. 冻结 Presentation Source、Schema、Registry、Compiler、Frozen Presentation Catalog 与独立 Fingerprint 契约；
2. 建立 Asset Registry 与最小窗口、渲染、字体、图片和输入后端；
3. 建立通用控件树、布局、层级、事件、数据绑定和动作绑定；
4. 建立 Map Widget、区域索引资源、Entity ID 拾取和 Query 驱动着色；
5. 在 `Dillen-Game` 中加入小型原创地图、政治实体与 Presentation Package，并接入 Demo 0.5 机制；
6. 建立 `demo_0_8_playable_integration_probe`，覆盖无窗口的绑定/命令测试和有窗口后端的最小冒烟测试；
7. 最后执行 Save / Load、Migration、长周期运行与 Replay 产品级验收。

**完成情况（2026-09-02，经审查修正）**：**第 1 条未达成，第 3 条只达成一半**。
先前此处写作「1–6 均已达成」，是错的，改正如下。
第 1 条只做出了独立 Presentation Fingerprint，**Presentation Source → Schema →
Registry → Compiler → Frozen Presentation Catalog 这条链一节都没有建**：
运行期仍由 `ControlTree::Load()` 现场解释一棵可编辑的字符串树。
第 3 条的控件树、布局、事件、数据绑定成立，但**动作绑定不通用**——见 §4.4.5。
2、4、5、6 达成，另有两处偏差：
第 5 条的“小型地图”已作废（§4.4.1 第 1 条），接的也不是 Demo 0.5 的机制
而是新写的地图机制包（§4.4.1 第 4 条的偏差记录）；
第 6 条的 `demo_0_8_playable_integration_probe` 没有建成**一个**探针，
而是拆成五个无头探针（`map_command_probe`、`client_state_probe`、
`presentation_binding_probe`、`control_tree_probe`、`text_atlas_probe`）
加一个需要 GPU 的 `map_renderer_smoke`。拆开是因为它们能进标准套件而它不能。

#### 4.4.4 验收门禁

- 不重新编译引擎即可更换地图 Content、Presentation Package 或允许替换的 Mechanism / Root Package；
- 地图拾取在缩放、窗口重建和读档后仍稳定返回同一 Entity ID；
- GUI 数据只来自同代际 Query Snapshot，按钮只产生通用 Command / Capability Invocation；
- 关闭或替换 GUI 后世界继续运行，重新打开时显示由当前 Snapshot 重建；
- Presentation Source 缺失、资源损坏、Binding 指向不存在的 Contract 或非法动作时，加载明确拒绝或隔离，不污染 Authoritative World；
- 同一 Ruleset、初始状态、输入序列和 RNG Seed 在无界面 Probe 与窗口 Host 中产生相同最终 Save、Fact Stream 和 Replay Checksum；
- Save / Load 覆盖 Entity、Component、Relation、Mechanism、Clock、RNG、Inbox、Queue 和 Sequence；读档后派生索引与 Snapshot 可重建；
- 至少一条 Schema Migration 完成迁移后继续运行对拍，而不只验证迁移字节可读取；
- 使用实际可玩切片执行长周期 Tick、多个存档检查点和固定 Command Log Replay；
- Kernel、World、Runtime 和通用 Host 中不存在地区、政治实体、经济、科研、生产等 Demo 业务硬编码。

**验收门禁逐条状态（2026-09-02）**。这张表是刻意写细的：原 Demo 0.8 纵向切片的
「已完成」与「已被门禁挡住」不是一回事，把两者混在一起正是封存时最容易出问题的地方。

| 门禁 | 状态 | 依据 / 缺口 |
| --- | --- | --- |
| 不重编译即可更换地图 Content / Presentation Package | **已达成** | §4.4.9；`main.cpp` 里只剩 Root Ruleset 名与包路径，机制 / Definition / 字段 / 实体类型 / 组件 / 角色全部来自包（`subject_role` 进 Binding，实体类型与组件字段从 id 表资产读回） |
| 拾取在缩放后稳定返回同一 Entity ID | **已达成** | §4.4.7；`MapEntityIndex` 从随栅格发布的 id 表与世界里的 `source_id` 拼出对应，门禁是对调 id 表两项后 Entity 必须跟着换 |
| GUI 只读同代际 Query Snapshot | **已达成** | `PresentationView` 拒绝空 / 未发布 / 不前进的快照；`presentation_boundary_probe` |
| 按钮只产生通用 Command | **已达成** | §4.4.7；按钮声明 Capability 契约 + 契约自己声明的 operation，三道独立检查（包声明过 / Ruleset 发布了 / 实例公开提供），未用冻结的 8 个 Command 变体之外的东西 |
| 关 GUI 后世界继续运行 | **已达成** | §4.4.10；`presentation_lifecycle_probe` Tick 4 销毁整个表现层、Tick 9 重建，世界与从未被观察过的一份逐字节相同，重开的界面与全程未关的一致 |
| Presentation 缺失 / 资源损坏 / Binding 悬空被拒 | **已达成** | `presentation_binding_probe`、`map_index_raster_probe`、`text_atlas_probe` 各自注入验证 |
| 无界面 Probe 与窗口 Host 产生相同 Save / Replay Checksum | **已达成** | §4.4.10；`map_renderer_smoke` 同一命令日志跑两遍，一遍每 Tick 绘制拾取 present，4074093 字节完全相同 |
| Save / Load 覆盖全部子系统 | **已达成** | §4.4.10；`world_map_durability_probe` 在 14187 实体世界上，四个检查点各自存档载入续跑，与直通逐字节相同 |
| 至少一条 Migration 迁移后继续运行对拍 | **已达成** | §4.4.10；带旧指纹的镜像迁移恢复后继续跑到底，落在同一份字节上 |
| 长周期 Tick、多检查点、Command Log Replay | **已达成** | §4.4.10；一份固定 Command Log，四个检查点加一次 DeterministicReplayService 重放，全部落在同一份 4074093 字节存档上 |
| Kernel / World / Runtime / 通用 Host 无业务硬编码 | **已达成** | `architecture_guard_probe`；地图机制包是纯 DSL，引擎侧零行 |
| 通用 Presentation 层无业务硬编码 | **已达成** | §4.4.6 + §4.4.7；`adjust_level` / `AdjustLevel` / `province_panel` / `namePrefix` 均已移出，`src/presentation` 里只在注释中留有历史说明 |
| Presentation 经 Source → Compiler → Frozen Catalog | **已达成** | §4.4.6；Schema Registry + PresentationCompiler + FrozenPresentationCatalog，`control_tree_probe` 13 处加载期拒绝，六处注入全部命中 |

#### 4.4.5 审查发现（2026-09-02）：原 Demo 0.8 纵向切片不能封存

以下五条由人工审查提出，逐条对照代码后**全部成立**。它们的共同点值得先说：
**每一条都能通过全部 38 个探针**。这不是探针写得不够多，而是它们守的是
「这套代码做的事对不对」，而没有一条守「这套代码做的是不是通用的事」。
一个把业务动词写死在通用层的实现，行为完全正确、门禁全绿、并且从根上违背 §1
的可定义性主张。

**1. 业务动作硬编码进通用 Presentation 层。**
`control_tree.cpp` 里按钮的 `action` 必须字面等于 `adjust_level`；
`map_command.hpp` 的 `MapIntent::Kind` 是一个只含 `AdjustLevel` 的枚举；
`map_command.cpp` 据此产生 `MechanismCommand`。
公开的 Command / Capability Contract 被整条绕过。
我在 §4.4.4 里把「按钮只产生通用 Command」记成已达成，是**过度声称**：
`ControlTree` 够不着世界这一点成立，但它产出的东西本身就是业务专用的。

**2. Presentation 没有在加载期编译。**
运行期由 `ControlTree::Load()` 现场解释一棵可编辑字符串树。
§4.4.3 第 1 条要求的 Presentation Source → Schema → Registry → Compiler →
Frozen Presentation Catalog 一节都没建，只做出了独立 Fingerprint。
P5b 当时把「Kernel 只存无类型的树、由表现层解释」当作优点写进备忘录——
那个判断在**避免 Kernel 学到 UI 词汇**这一点上仍然对，
但它不能替代编译期冻结，我当时没有把这两件事分开。

**3. 地图拾取不返回稳定 Entity ID。**
`PickAt()` 返回稠密 `uint16_t` 栅格索引；实体是之后靠
`namePrefix + std::to_string(index)` 拼名字找回来的。
索引↔实体的对应因此依赖 Spawn 顺序与命名约定这两个**约定**，而不是数据。
§4.4.4 明写「读档后仍稳定返回同一 Entity ID」，现在无法成立。
P4a 当时特意让 `Resolve()` 读角色绑定而不做算术，理由正是
「实例 id 与省份序号平行是 Spawn 顺序的巧合」——同一条理由适用于这里，我没有推下去。

**4. Demo 宿主硬编码地图业务绑定。**
实体类型、组件名、机制名、Definition 名、字段名与 Root Ruleset 全部写在
`apps/map_viewer/main.cpp`，所以「不重编译更换 Presentation 包」只在
**结构完全相同**的包之间成立。

**5. 通用 Overlay 硬编码面板 ID。**
`overlay.cpp` 里只有 `control.id == "province_panel"` 的面板才画背景。
重命名配置不会报错，只会静默不画——正是 P5b 立下「未知键必须拒绝而不是静默忽略」
那条规矩要防的失败模式，我在同一个文件里又犯了一次。


**当前基础状态（2026-09-03 实测）**：核心 Durability 闭环已由 `persistence_replay_probe` 达成；`standalone_host_probe` 已覆盖 Platform 文件写入、同目录临时文件原子替换和恢复；`demo_0_5_vertical_slice_probe::CheckSaveResumeEquivalence()` 已实现 Tick 1—20 直通与 Tick 12 存档后续跑到 Tick 20 的逐字节对拍。启用 Presentation 与 GL 地图后端的 `dillen-map-renderer-windows-x64` 配置在 Debug / Release 均为 **44/44**（标准套件 43/43，纯 Standalone `dillen-headless-windows-x64` 28/28），`map_renderer_smoke` 在有图形设备的实机中通过而非跳过。Presentation 纵向管线、窗口渲染后端、Map Widget、独立 Presentation Fingerprint、稳定 Entity 拾取、最小输入闭环、文字渲染与选择高光均已达成。一个可操作的窗口 `dillen_map_viewer` 已可运行。Save / Load、Migration 续跑、长周期 Tick、多检查点与固定 Command Log Replay 已在 14187 Entity 地图世界上达成（§4.4.10，`world_map_durability_probe`）。**GUI 关闭/重建**（`presentation_lifecycle_probe`）与**窗口 Host 与无头路径的存档对拍**（`map_renderer_smoke`）也已达成（§4.4.10）。**§4.4.4 的全部验收门禁至此达成，地图基础可以正式冻结。**



#### 4.4.7 R1/R2：稳定 Entity ID 与公开 Capability 契约（已完成，2026-09-02）

§4.4.5 第 1、3、4 条的修复。

##### R1：拾取返回的是身份，不是图上的位置

`PickAt()` 仍然返回 16 位稠密栅格索引——那是 ID 附件装得下的东西，也是调色板的寻址方式。
问题从来不是索引，而是**把索引变回实体的方式**：

```
StableEntityId(namePrefix + std::to_string(index))
```

这不是查找，是「内容碰巧是这么拼的」这条假设。改名、换 Spawn 顺序、换语料重新生成，
世界照样加载、Tick、存档，而每一次点击都落在错误的省份上。

新的 `MapEntityIndex` 用**两半数据**拼出对应关系，各自待在该待的地方：

| 半边 | 来源 | 理由 |
| --- | --- | --- |
| index → source_id | 随栅格发布的 `map_province_ids` 资产（独立载荷 + 摘要，57 KB） | 它描述的是**栅格的编码**，属于表现层 |
| source_id → Entity | 走 `ComponentQuerySnapshot::FindOwners()` 读世界 | 世界本来就带 `source_id`，这半边合法地属于它 |

两半都不是约定。`ClientState::selected/hovered`、`MapIntent`、`MechanismPanel::Read`
全部改成按 `EntityId` 寻址；`ProvinceProjection` 的行仍是调色板的行号，但行 → Entity
的表来自 `MapEntityIndex` 而不是重新拼名字。

**这条门禁必须特别设计**，因为这份内容里命名约定**恰好成立**——把实现换回拼名字，
上面每一条断言都照样通过。唯一能分辨的办法是**让约定为假**：把 id 表里第 11 项和
第 4096 项对调，重新解析，两个索引解析到的 Entity 必须跟着换。走命名约定的实现对此
毫无反应。注入验证：换回拼名字后，探针报
「swapping two entries of the id table did not swap the Entities」。

##### R2：按钮说的是契约，不是动词

`MapIntent::Kind::AdjustLevel` 没有了。此前它是这个通用头文件里一个只含一个业务值的枚举，
在控件编译器里跟字符串 `"adjust_level"` 比较——Demo 的词汇写进了引擎，公开契约被整条绕过。

**一条硬约束先说清楚**：`MechanismCommandOperation` 的 8 个变体是**冻结**的，
不能为了 UI 加第 9 个。所以路线不是发明新的 Command，而是用已有的机器：

按钮在布局里声明 **Capability 契约 + 契约自己声明的 operation + 字段 + 增量**：

```
button = { text = "+1"
           capability = dillen.map.site_development
           capability_version = 1
           operation = adjust_level
           field = level
           amount = 1 }
```

**三道各自独立的检查**，缺一不可：

1. **包声明过要用它**——`requires` 里必须有同名同版本的 `capability` 条目，否则编译期拒绝。
2. **Ruleset 确实发布了它**——`FindCapability(id, version)`，且 `operation` 必须在
   契约自己的 `operations` 里，否则包可以随便发明动词而契约形同虚设。
3. **被命令的实例公开提供它**——`MapCommandTranslator::Translate` 比对该 Definition 的
   `providedCapabilities`。这一条是前两条都盖不住的：**一个从未声明提供该契约的机制，
   UI 就是够不着，名字对得再齐也没用。**

通用层现在知道的是 capability、operation、字段、增量——全是 Kernel 概念；
它不再知道「生产等级」是什么。`MapCommandSpec` 从七个字段缩到两个
（Definition + 角色名），而 Definition 来自编译好的视图。

##### 又一次「因为错误原因通过」，而且犯了两遍

第一版的「`requires` 对 capability 是承重的」测的是 `dillen.map.undeclared`——
这个名字 Ruleset 里也没有，所以是 Ruleset 查找先失败，对 `requires` 什么都没说。
把 `declared` 检查注入掉，断言依然全绿。

改成「删掉声明、保留使用」之后**还是不discriminating**：版本号原本取自 requires 条目，
声明一删版本变成 0，Ruleset 在版本 0 上查找失败——两道检查坍缩成了一道。

真正的修法是让两道检查**互相独立**：版本号改由**控件自己声明**，
Ruleset 那道题就在控件要的版本上回答，`requires` 那道题单独问。
这之后同一注入立刻报错。

这是本项目第三次栽在同一件事上（角色写入没进黄金夹具、`requires` 对字段、
`requires` 对 capability）。**判据已经很清楚：一条拒绝性断言，必须让被测的那道检查
成为唯一可能失败的那道。** 否则它只是在测别的东西。

##### 门禁

`control_tree_probe`：**17 处编译期拒绝**（新增：契约未声明的 operation、
包未声明使用的 capability、无人听说过的 capability），加 id 表对调、
Entity 往返、以及从另一侧验证的契约检查（伪造一个 Definition 未提供的 capability，
必须被 `CapabilityNotProvided` 拒绝）。
`map_command_probe`：14187 个省份仍全部解析，两条路径 4074093 字节存档逐字节相同。

**注入命中**：命名约定回归、契约提供未检查、operation 未检查、`requires` 未查。


#### 4.4.8 性能与边界审查（已完成，2026-09-02）

五条人工审查发现，逐条修正。

##### 1. 每帧同步拾取造成流水线停顿

悬停要连续采样，而 `PickAt()` 是同步回读：CPU 向 GPU 要一个它还没画完的像素，
然后阻塞等它画完——**为了移动一个高亮，每帧一次完整流水线停顿**。

改成**两个 Pixel Pack Buffer 轮转**：`RequestPick()` 把回读发进其中一个（此时
`glReadPixels` 的目标指针是缓冲内偏移而不是内存地址，调用变成异步的），
下一次调用去 map 另一个——至少一帧前填好的那个。一个缓冲不行：map 它就等于等
刚刚发进去的那次传输。

**两个入口而不是一个**：`RequestPick`/`LastPick` 的答案晚一两帧，对光标不可见；
点击必须精确，所以点击仍走同步的 `PickAt`。

踩的坑:PBO 只按像素大小分配了 2 字节，而 `GL_PACK_ALIGNMENT` 默认 4 会把一行补齐，
传输**静默什么都不写**，异步拾取一直返回 0。现在分配 4 字节并显式把对齐设为 1——
两处都做，让它不依赖任何一处写对。

##### 2. 窗口不可调整大小

`SDL_WINDOW_RESIZABLE` 加上，但真正的工作是 `BuildTargets()`：
颜色、ID、深度三个附件按新尺寸重建。只加标志不重建比固定尺寸**更糟**——
地图会画进旧尺寸的缓冲，拾取会读到光标已经不在的像素。
监听的是 `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED`（drawable 尺寸，不是窗口尺寸：
高 DPI 屏上两者不同，而这里每一个像素量都是 drawable 坐标）。
重建时把在途的拾取请求作废——它是对旧附件发的，答案指向一个不再存在的像素。

##### 3. GL 冒烟测试进 CTest

此前它**不在清单里**且在没有 GPU 时返回 0。两件事叠起来的后果是：
一台**能**跑它的机器也从来不跑，渲染后端的回归可以一路绿到底。

现在注册进 CTest，并用 `SKIP_RETURN_CODE 77`：没有 GPU 或没有显示器时退出 77，
CTest 报「Skipped」——这是唯一诚实的答案。返回 0 让「跑不了」和「跑通了」无法区分。
渲染器配置现在是 **44/44**（当时 39/39）。

##### 4. 字体图集只支持 ASCII 32–126

三样东西：**UTF-8 解码**、**任意且可不连续的码点区间**、**分行装箱**。

区间用 `codepoints = 32-126,0x4E00-0x9FFF` 声明，因为有意义的区间**不连续**：
中文界面要拉丁标点和 U+4E00 起的一块，中间两万个码点一个不要，单个 first/last
说不出这件事。

装箱改成 shelf：一行放满就换行。图幅上限 **4096×4096**（一张即可——
常用汉字约两万个字形，UI 尺寸下约 540 万覆盖像素，对 4096 方阵的 1670 万绰绰有余；
排成一行则是 25 万像素宽，根本不是任何纹理）。装不下**明确拒绝而不是截断**——
静默丢掉区间尾部，正是「字体恰好缺了没人测过的那些字」的来源。
上限可由包声明（`atlas_max_width`），既为纹理限制更小的设备，也为了让换行本身可测。

**字体没有的码点必须拒绝**：`FT_Load_Char` 对未映射码点会成功返回 .notdef 空心框，
接受它等于允许包声明任意区间然后得到一堆方框——正是这一层在别处一直拒绝的替换。

##### 5. 没有真正的 `DILLEN_BUILD_PRESENTATION` 边界

「表现层可选」此前只是**说法**：纯 Standalone 配置照样编译 FreeType 和整个表现层。
现在是一个真实配置——`DILLEN_BUILD_PRESENTATION=OFF` 下没有表现层目标、
没有依赖它的探针、也不配置任何只为它存在的第三方源码。
实测：纯 Standalone **28/28** 且构建树里没有 `third_party`；完整配置 38/38。

这条让 §4.4.0.4 的「可删除性」从断言变成**可检验的**：它不再是「没人链接它」这句话，
而是一个每次都能构建出来的配置。

##### 门禁与注入

`map_renderer_smoke` 新增：三次 resize 后中心与角落像素仍可寻址、
异步拾取的结果必须等于同一像素的同步结果。
`text_atlas_probe` 新增：UTF-8 解码七个用例（含截断与非法续字节，
畸形输入只花一个替换字形而不是一个不终止的循环）、不连续区间、
区间之间的空隙真的是空隙、窄图幅强制分行后字形框互不重叠且都在图幅内、
字体缺失码点被拒、畸形区间列表被拒。

**注入命中**：不换行（用 256 宽图幅强制才测得出——默认 4096 下拉丁区间根本不换行，
只能用默认值的门禁无论换不换都会通过）、接受 .notdef、UTF-8 按字节解码。


#### 4.4.9 第二轮审查修正（已完成，2026-09-02）

##### 1. 宿主仍写死包里的名字

`dillen.map.region`、`dillen.map.geography/source_id`、角色 `province` 仍在
`main.cpp` 里。前两个其实**已经在 id 表资产的属性里**（R1 放进去的），
只是宿主没读回来；角色则完全没有出口。

- 角色进 **Presentation Binding**：`ui_binding` 的 `properties` 新增
  `subject_role`，编译进 `CompiledPresentationView::subjectRoleName`，
  宿主用 `tree.SubjectRole()`。此前那个字符串字面量意味着一个包只能被
  「用同一个词表达同一个意思」的包替换。
- 实体类型与组件字段从 `MapEntityIndex::Spec()` 读回——那是 id 表资产自己的声明。

**现在 `main.cpp` 里剩下的包名只有 Root Ruleset 名和包路径**，
其余机制 / Definition / 字段 / 实体类型 / 组件 / 角色全部来自包。

##### 2. 稳定 ID 转换发生在宿主而不是 Map Widget 内部

`PickAt()` 返回 uint16 稠密索引，每个画地图的宿主各自把它转成实体——
把地图上**唯一不是位置的东西**（身份）放到了产生它的控件之外，还让每个宿主重复一遍。

`MapRenderer::SetEntityIndex()` + `PickEntityAt()` / `LastPickedEntity()`：
转换回到控件自己的 API 里。`PickAt` / `LastPick` 保留给后端自己的门禁——
它们必须能断言栅格索引，因为那才是 ID 附件里装的东西。

##### 3. 调色板固定 128×128

那是一个**渲染侧的 16383 上限**，内容看不见、没人报告，
多一个省份就会静默绕回到别人的颜色上。

尺寸改为由省份数推导（每省一格 + 第 0 行「无省份」，向上取到 2 的幂），
着色器里的 `128` 换成 `uPaletteSide` uniform，调色板边长和容量由
`PaletteSide()` / `PaletteSize()` 告诉调用方。

**门禁放在无头探针里而不是 GPU 冒烟里**，因为这条性质是算术而不是 GPU 行为——
而且这份地图只有 14187 个省，128×128 = 16384 恰好够用，**冒烟测试根本分辨不出来**
（把边长改回固定 128 注入，冒烟照样全绿）。
`PaletteSideFor()` 做成自由函数，`province_projection_probe` 用
0 / 1 / 255 / 14187 / 16383 / **16384** / 16385 / 65534 各测一遍：
既够用、是 2 的幂、又不比需要的大一倍。同一注入立刻报出 9 处失败。

**剩下的真实上限被明确拒绝而不是绕回**：ID 附件是 R16UI，最多 65534 个省份，
超过时 `Open` 直接拒绝并说明——抬高它是格式变更（R32UI 加更宽的回读），不是改个常数。

##### 4. 资源路径缺 Windows 规范化检查，摘要只查长度

路径检查只认识 `/` 和 `:`，而这个解析器**在 Windows 上也跑**——
`..\secrets.bin`、`skins\..\secrets.bin`、`\\host\share\r.bin` 在真正能解析它们的
系统上全部放行。现在按**段**检查（而不是整串找 `..`，那会误伤 `world..v2.bin`
这种合法文件名）：反斜杠也是分隔符、盘符、UNC 根、空段、`.` 段、`..` 段一律拒绝。

摘要此前只查 64 个字符——所以六十四个字符的散文是合法声明，
要到后面比对字节时才失败，而那时的报错说的是「字节不匹配」而不是「声明有问题」。
现在逐字符验 `[0-9a-f]`，且**只接受小写**：大写是同一个摘要的第二种拼法，
而需要归一化的比较就是会忘记归一化的比较。

端到端触发的诊断码 22 → **30**。

##### 门禁与注入

四种配置全绿（当时的数字；现为 43 / 44 / 28，见 §4.4.10）：完整 **38/38**、渲染器 **39/39**、纯 Standalone **28/28**、Debug 38/38。
`map_renderer_smoke` 新增：控件的实体拾取（同步与异步）必须与它自己的栅格索引一致、
调色板容量必须大于省份数。
`province_projection_probe` 新增调色板边长推导。
`authoring_diagnostic_contract_probe` 新增八条路径与摘要拒绝。

**注入命中**：控件的实体拾取偏移一位、调色板边长写死 128（在无头探针里，
GPU 冒烟测不出来）、路径与摘要的每一条拒绝。


#### 4.4.10 第三轮审查修正与收尾（已完成，2026-09-03）

##### 1. 地图规模的耐久性门禁

Save / Load、Migration、Replay 全都只在 Demo 0.5 那个三实体世界上证明过——
那是对**代码**的真实证明，对**规模**则什么都没证明。只在规模下出现的失败，
恰恰是没人为它写小测试的那些：溢出的下标、某个尺寸以上会重排的容器、
把一次检查点变成一分钟的按实例成本。

新增 `world_map_durability_probe`，在 14187 实体世界上：

- **一份固定 Command Log**（8 条，跨全图、跨全程，含同省重复与延后一 Tick 的调度，
  所以存档时 Inbox 里带着状态而不是恰好为空）
- **四个检查点**（Tick 4 / 8 / 12 / 16）各自存档、载入新会话、续跑到 20，
  必须与直通跑出**逐字节相同**的存档
- **Replay**：同一份 Log 经 `DeterministicReplayService` 走另一条代码路径，
  终局存档必须相同；跑两遍必须完全一致
- **Migration**：带旧指纹的镜像经迁移恢复后**继续跑到底**，落在同一份字节上

最后一条的门禁不是「迁移后的字节能读」，而是「经过迁移的世界**继续运行**后
落点与从未迁移过的一致」——那才是玩家在乎的，也正是只查格式的检查会漏掉的。

**注入命中**：续跑时漏掉一个 Tick、迁移后少跑一个 Tick。
注意其中一条的输出：**字节数完全相同（4074093 = 4074093）而内容不同**——
只比数量的断言会全过。

##### 2. 生成器混业务

`province_content_emitter` 一次调用产出四个包：地理、生产机制、算法、
Capability 契约和一个两按钮面板。那是一个 demo 生成器，
用「地图 Adapter」的名字发布它，意味着以后每一张地图都会焊着一份 demo 玩法出厂，
而唯一发现这件事的办法是读九百行。

玩法降级为**可选的 `DemoProductionSlice`**，按名字说明它是什么。不给它，
产出就是一张地图：契约、地区、边界、栅格、id 表、能加载它们的 Ruleset，
**别的什么都没有**——不是空的机制包，是文件根本不存在。

`province_map_emitter_probe` 加载的正是这个产物。**关键是那条否定断言**：
「地图能加载」对一个偷偷仍然生成机制的实现同样成立，
「世界里有零个机制实例」不会。

拆分后重新生成 `Dillen-Game`，内容摘要 `c606a314…` **逐字节未变**。

##### 3. 没人用的 MapPoint 渲染目标

正式宿主用省份质心做缩放锚点，(u,v) 附件每帧照写。改成 `mapPointReadback`
**默认关闭**：产品路径一分不付，冒烟测试显式打开——它是唯一能精确回答
「这个像素在地图的哪里」的东西（任意曲率下形变不可解析求逆），
用来断言居中后屏幕中心显示的就是相机经度。关闭时 `DrawBuffers` 该槽传
`GL_NONE`，着色器仍写但被丢弃，一个着色器服务两种配置而不是两个会各自漂移。

##### 4. 收尾

- **质心性能**：经度只取决于 x，原实现却对 1210 万像素逐个调 `sin`/`cos`
  去算 5616 个不同的答案。改成按列查表（90 KB），全尺寸构建 **17 ms**，
  并加了预算门禁——它防的是数量级，不是百分比。
- **质心自检**：声明尺寸与解码像素数不符会越界索引，
  「加载器本该拦住」不等于拦住了；现在直接拒绝。
- **状态文字**用 `client.viewportHeight` 而不是初始高度，缩放后跟着走。
- `map_renderer_smoke` 的 C4456 遮蔽（两处 `picked`）改名消除，
  全部配置零警告。
- **正式 Preset**：`dillen-map-renderer-windows-x64` 与
  `dillen-headless-windows-x64`（`DILLEN_BUILD_PRESENTATION=OFF`），
  各带 Debug/Release 的 build 与 test preset。四种配置不再依赖手敲
  `-D` 参数。

##### 门禁

**43/43**（标准）、**44/44**（渲染器）、**28/28**（纯 Standalone）、
Debug 43/43，四种配置零警告。

##### 5. §4.4.4 的最后两条

**GUI 关闭与重建**（`presentation_lifecycle_probe`，无头）。这条听起来是一件事，
其实是两件不同的失败：世界会不会依赖表现层；以及重开的界面会不会拿着它自己留下的
旧状态。

做法是两个会话跑同一份命令，其中一个的**整个表现层被建起、销毁、再建起**——
Tick 4 关掉，Tick 9 重开。两条断言：

- 被看过、关掉、重开、又看过的世界，与**从没有人看过**的世界**逐字节相同**
- 重开的界面显示的内容，与**全程没关过**的界面**完全一致**——
  包括它缺席的那四个 Tick 和第二批命令

还有一条防空比较的断言：五个被观察省份都必须显示非零 level，
否则上面那条可能只是在比对两组空白。

**窗口 Host 与无头路径的存档对拍**（`map_renderer_smoke`）。这条只能在这里做——
套件里其他所有对拍比的都是**两条无头路径**，对「绘制会不会改变世界」什么都没说。

它可能会：后端每帧读一次快照，一个握着可变引用、或为拿快照而自己 Tick、
或捡起了自己的命令的渲染器，会产出一个取决于「有人看了多少次」的世界。
所以同一份命令跑两遍——一遍每 Tick 绘制、拾取、present，一遍没人看——
比字节：**4074093 字节完全相同**。

至此 §4.4.4 全部门禁达成，地图基础可以正式冻结。

#### 4.4.6 R3：Presentation Source → Compiler → Frozen Catalog（已完成，2026-09-02）

§4.4.5 第 2 条与第 5 条的修复。先做 R3 而不是放到下一阶段，是因为它重排整个
表现层的加载路径，越晚做返工越大。

##### 唯一的架构分叉：Schema 用代码还是用 DSL

本项目其他每一种 Schema 都是数据（Component、Relation、Mechanism），显然的做法是
再加一种 `.dcontrolschema`。**这个做法是错的**，理由具体而不是口味问题：

Component Schema 声明的形状，Kernel 能据此**通用地**存储、比较、迁移——数据带来了能力。
控件不是这样：`panel` 要有人排版、`button` 要有人绘制，那个「有人」就是代码。
所以数据 Schema 只能声明布局引擎**已经实现**的子集，它声明而引擎没实现的种类还必须在
加载期拒绝。也就是说数据不带来任何代码没有的能力，它只是**同一套语义的第二份实现**，
靠手工保持同步。

本项目已经因为同一条理由拒绝过两次（声明式 vs 受控脚本；在 GLSL 里重写形变），
沉淀下来的规矩原样适用:**一套语义只有一个真相来源**。

数据能定义的是数据真正能定义的东西——**布局**：有哪些控件、怎么嵌套、绑什么、显示什么。
那正是 Presentation 包所写的，也正是 Compiler 拿 Schema 去校验的对象。

所以 Schema 是一个**有类型的注册表**而不是一串手写 `if`：校验变成查表，属性有类型和默认值，
拼错的属性会带着名字和期望类型被报出来，编译产物按**槽位**而不是字符串寻址。

##### 管线

```
Presentation Source  （.dasset 的 content 块，Kernel 视为无类型的树）
      │  PresentationCompiler，对着 Schema Registry 与 Frozen Runtime Catalog
      ▼
Frozen Presentation Catalog  ── 不可变、按 id 寻址、没有字符串
      │
      ▼
ControlTree  ── 只剩布局与命中测试的整数算术
```

`control_tree.cpp` 里现在**一处字符串比较都没有**：控件种类是索引，属性是已知槽位上的
类型化值，绑定字段是 `MechanismFieldSlotId`。

##### 两个身份，两者都必须记

编译产物只对**它编译自的那些源**、以及**它解析时所对的那个 Ruleset** 有意义——
字段槽位就是那个 Ruleset 布局里的下标。所以 Catalog 同时记 `PresentationFingerprint`
（Kernel 早就算的那个，不另发明第二个哈希）与 `RulesetFingerprint`，
`ControlTree::Bind` 逐个核对。**绑到另一个世界会读到属于别人的真实数字，比读不到更糟。**
门禁用的第二个世界是历史 `dillen_demo_1_0` 工程夹具，是真的另一套 Ruleset，不是伪造的副本。

##### `requires` 从装饰变成真正的承重

`value` 控件绑定的字段必须出现在同一资产的 `requires` 里，而**那条声明正是 Mechanism 与
Definition 的来源**——所以宿主不再需要知道机制叫什么、Definition 叫什么、字段叫什么。
`MechanismPanel::Bind` 的签名从「三个字符串字面量」变成「一个 DefinitionId 加一组已解析槽位」。
这同时解决了 §4.4.5 第 4 条的大半。

##### 第 5 条：画不画底由配置声明

`background = yes` 成为 Schema 里的属性。此前是 `control.id == "province_panel"`，
重命名根面板不会报错、只会静默不画。门禁是**把布局里每个 id 都改名后重新编译，
画底的控件数量必须不变**。

##### 门禁（`control_tree_probe`，全程无头）

Schema：四个种类、冻结语义、重复种类与重复属性被拒。
Compiler：**13 处加载期拒绝**——拼错的控件种类 / 拼错的属性 / padding 不是数 /
size 既不是像素也不是 fill / axis 不是轴 / 布尔既不是 yes 也不是 no /
value 没有 field / 绑了没人听说过的字段 / **绑了 Ruleset 有但包没声明的字段** /
label 带子控件 / 两个根 / 未冻结的 Schema / 未冻结的 Runtime Catalog。
编译产物：树是扁平的且父在子前、每个兄弟块连续、字段槽位与 Ruleset 逐一相符。

**六处注入全部命中**：未知键静默忽略、`requires` 松匹配、子控件不受限、
兄弟块不预留、不核对 Ruleset 指纹、槽位常量被挪动。

##### 三处自己踩的坑，都值得记

1. **扁平化写错了**。深度优先追加会让兄弟节点不连续——A 的孩子插在 A 和 B 之间，
   于是 `firstChild + childCount` 指到了孙子。探针立刻报「control 5 escapes its parent」。
   修法是**先把整个兄弟块占位再递归**。
2. **`assert` 在 Release 里会被编译掉**。槽位常量的守卫如果用 `assert`，
   在唯一有人跑的构建里就是没人守的。改成无条件检查，失败时把注册表清空且不冻结，
   之后每一次使用都会大声失败。
3. **「`requires` 是承重的」那条断言原本测的是错的东西**。它绑的 `throughput` 在
   Ruleset 里也不存在，所以先在 Ruleset 查找上失败，对 `requires` 检查什么都没说——
   把 `RequirementFor` 松成「任意一条 mechanism_field 都算」，那条断言依然全绿。
   改成删掉 `output` 的**声明**但保留布局对它的绑定（Ruleset 里确实有这个字段），
   同一注入立刻报错。**一个因为错误原因通过的断言，比没有断言更糟。**

### 4.5 通用 Gameplay Authoring 能力收口

本阶段不制作产品 Demo，而是用六类机制模板作为**表达力证明**。Kernel 不得出现六类机制的专有名称；所有测试 Package 删除后，Kernel 的公共类型和执行路径必须仍然自洽。

最低能力面：

- 结构化静态 Definition：标量、引用、列表、对象、类型化集合、默认值、约束和版本；
- Entity / Component / Relation / Mechanism 的稳定引用和跨文件解析；
- Definition 继承或组合必须选择一种明确语义；在语义冻结前宁可拒绝，也不提供隐式深合并；
- 对 Definition、Entity、Relation 和 Mechanism 的统一 Query 操作数；
- 可被 Trigger、Effect、Algorithm 和 Presentation Binding 共同引用的字段槽位；
- 所有静态源进入 Package Ownership、Source Lock、闭包裁剪和 Frozen Catalog。

验收：同一套 Kernel 二进制装载六组完全外部的机制模板；删去任一包、替换实现包或替换 Root 时，未被选择的定义和程序不会进入 Frozen Catalog，合法替换不要求重新编译引擎。

### 4.6 Trigger / Effect / Scope 语言与语义层

这三类概念属于通用 Gameplay Authoring 语言，不属于 HOI3 兼容层，但也不能成为绕开现有 Algorithm Runtime 的第二套执行器。

**Scope**：

- 表示类型化求值上下文、当前 Subject、命名角色和确定性目标集合；
- 支持通过公开 Relation、Component、Definition 和 Mechanism Role 遍历；
- 每次上下文切换都具有静态输入/输出类型，空集合、单值和多值规则必须明确；
- 集合遍历使用稳定 ID 排序，禁止依赖容器地址或导入源偶然顺序。

**Trigger**：

- 是无副作用、可组合、可缓存的布尔程序；
- 支持比较、逻辑组合、存在、全称、计数和确定性归约；
- 只能读取同代际 Query Snapshot、静态 Definition 和显式参数；
- 不调 RNG、不排队事件、不修改 World。

**Effect**：

- 是产生权威意图的类型化程序；
- 只允许生成现有 World Command、World Transaction、Scheduled Event 或 Capability Invocation；
- 所有修改继续经过 Schema、权限、生命周期、引用和事务校验；
- 不直接持有 Store，不增加外交、科技、间谍等领域专用 opcode。

**共享语义管线**：Parser 只产生 Source AST；Semantic Resolver 负责类型、Scope、引用和 Contract 解析；Compiler 将合法语义编译为 Stable ID / Slot 化 Frozen Program；Declarative 与 Controlled Script 复用相同 Trigger、Scope 和事务下降语义。Source Span、诊断码和 Source Map 必须贯穿整个管线。

验收：同一 Trigger 在不同遍历/装载顺序下结果一致；同一 Effect 产生相同 Canonical Transaction；非法 Scope 转换、悬空引用、写入只读 Definition、跨包私有字段访问和非确定性遍历在加载期拒绝。

### 4.7 六领域参考机制门禁

六类内容是 Reference Gameplay Library 和测试样本，不是 Kernel 内建系统：

| 领域 | 最小机制模板需要证明的通用能力 |
| --- | --- |
| 科研 | 静态技术 Definition、前置条件 Trigger、进度状态、完成 Effect、解锁 Capability |
| 生产 | 配方/单位/建筑 Definition、资源输入 Query、队列或进度、产出 Transaction |
| 外交 | 双边或多边 Relation、提案/接受事件、条件 Trigger、关系变更 Effect |
| 政治 | 法律/制度/人物 Definition、资格条件、周期变化、状态切换和跨机制通知 |
| 情报 | 主体—目标关系、可见度分层、任务生命周期、受限 Query 与结果事件 |
| 战区 | 地理 Entity 集合、邻接 Relation 遍历、动态范围、聚合 Query 与任务分派 |

每个领域至少具有一个 Contract Package、一个可替换 Mechanism Package 和一个 Content/Spawn 样本。六者至少形成两条只经 Capability/Event/Relation Contract 的跨机制链，并完成 Save / Load、Migration、Replay、故障包拒绝和更换 Root 对拍。

验收重点不是玩法是否丰富，而是：若任何模板必须要求 Kernel 识别“科技”“国家”“外交”“战区”等名字，则本阶段失败，应回到通用原语分析。

### 4.8 HOI3 移植阶段

只有 §4.5—§4.7 通过后才开始。本阶段不是复刻 `hoi3_tfh.exe`，而是让独立工具链理解 HOI3 内容表达，并将其映射到已经存在的 Dillen 通用语义空间。

固定顺序：

1. 冻结通用 External Corpus Importer ABI、Normalized Source IR 容器和版本契约；
2. 实现 HOI3 VFS、编码、Clausewitz 文本、CSV、Lua 数据、地图和资源的来源规范化；
3. 优先导入 `common/` 静态规则与类型定义，验证静态 Definition 管线；
4. 导入国家、省份、外交、战争、单位和领袖等历史/初始状态；
5. 将 HOI3 Scope / Trigger / Effect 解析为 Normalized HOI3 Semantic IR，由 Mapping Profile 映射到 Dillen 原生 Scope / Trigger / Effect；
6. 在共享语义基础上导入 Event 和 Decision；
7. 最后处理原版 GUI / GFX、本地化与 Presentation 映射；
8. 使用 Oracle 只补充内容作者实际依赖、但文本本身无法确定的最小可观察语义证据。

Importer 不引用目标 Gameplay Contract；Mapping Profile 不读取原始文件；Kernel 不引用 HOI3 IR。成功导入只证明 Source / Mapping Compatibility，不自动证明 Gameplay 等价。

---

## 5. 当前阶段明确禁止的工作

- 继续堆叠 HOI3 Country、War、Diplomacy、Technology 等专用 Runtime State；
- 继续扩展 HOI3 Importer 语义切片；
- 在 Kernel 中增加 HOI3 专用 Query、Command、Capability 或 Setter；
- 让 Mapping Profile 解析 HOI3 原始文件；
- 让 Importer 引用 Dillen Gameplay Contract；
- 用 Oracle 逐 Tick 轨迹定义 Dillen Runtime；
- 在 Trigger / Effect / Scope 与静态 Definition 语义尚未冻结时直接移植 HOI3 Event、Decision 或 Gameplay 行为；
- 把科研、生产、外交、政治、情报或战区参考机制的字段、动词、状态机或 Query 写进 Kernel C++；
- 为赶进度在 Parser 中直接创建权威实例，或让运行时解释可编辑 Trigger / Effect / Scope 字符串；
- 用新的产品 Demo 编号替代 §4.1 的能力门禁；
- 为未来可能使用的功能提前扩大 Kernel 公共 API。

当前唯一主线是：**先补齐可由外部 Package 编写科研、生产、外交、政治、情报和战区机制所需的通用 Kernel / Authoring 能力，让静态定义文件进入版本化 Frozen Catalog，建立完整的 Dillen 原生 Scope / Trigger / Effect 前端与语义编译层；上述能力由纯 Dillen 六领域参考 Package 验收后，再启动 HOI3 Importer、Mapping Profile 与内容移植。地图纵向切片只保留为非阻塞维护与回归样本。**
