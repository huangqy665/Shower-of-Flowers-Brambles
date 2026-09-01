# Project Dillen 工程开发备忘录

> **权威状态：生效中。**本文是 Project Dillen 唯一的总体架构、系统边界、开发顺序与 Demo 验收指导。除源码、测试和版本化接口契约所证明的实现事实外，其他开发概览、讨论记录、逆向备忘录和历史设计文档均不具有架构裁决权。若其他文档与本文冲突，以本文为准。

## 0. 文档治理

### 0.1 权威范围

本文负责确定：

- Project Dillen 的产品定位、核心术语和不可破坏的依赖方向；
- Dillen Kernel、Gameplay Package、Ruleset、Content Package、Importer、Mapping Profile 与 Oracle 的职责边界；
- Authoritative World、Parser、Resolver、Runtime Compiler、Algorithm Runtime、Transaction、Persistence 等系统的实现边界；
- 当前阶段的建设顺序、Demo 范围和验收门禁。

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
4. Demo 里程碑必须通过前置门禁后才能开始下一阶段，不以堆叠功能数量代替纵向闭环。
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
| **Contract Package** | 公共 ABI：Capability Contract、Query / Command Contract、GUI 数据绑定契约、跨包共享的 Component / Relation Schema | 任何业务实现。没有 Algorithm，没有 Definition，没有 Spawn |
| **Mechanism Package** | 业务实现：Mechanism Template / Schema、Algorithm、以及**实现或消费** Contract 的绑定 | 不定义供他人依赖的公共 ABI（那属于 Contract Package）；不拥有具体世界数据 |
| **Content Package** | 具体世界：Entity / Mechanism Definition、Spawn、历史、初始状态、场景；对表现资源的**引用** | 不定义 Schema、Algorithm 或通用 GUI 行为 |
| **Presentation Package** | 表现实现：布局、贴图、字体、音频、本地化文本 | 不含 Gameplay 语义，不参与确定性状态 |

关键约束：**两个 Mechanism Package 不得互相依赖。**需要交互时，双方各自依赖同一个 Contract Package——这是 Demo 交付的 `dillen.demo1.contracts_package` 已经验证的形态（见 §3.8）。

GUI 的职责按同一条线切开，避免三处重复拥有：

- **Mechanism Package** 只声明 GUI **数据绑定与交互契约**（哪些字段可读、哪些命令可发）；
- **Presentation Package** 拥有全部**表现实现**（布局、贴图、字体、音频、本地化）；
- **Content Package** 只**引用**表现资源，不拥有通用 GUI 行为。

**角色不是身份的一部分（重要推论）**：`PackageRole` 只在 `PackageManifest` 上，**不进 `PackageLockEntry`，也不进 Ruleset Fingerprint**。因此：

- 角色约束是**加载期检查**，由严格 Authoring 模式（`requireExplicitPackageRoles`）执行，默认关闭；
- **存档里不含角色信息**，读档时无法回溯验证当初的包角色是否合规——存档校验的是 Package / Source Lock 与 Fingerprint，与角色无关；
- 因此不得把"存档能加载"理解为"角色边界当初被遵守过"。要让角色成为可追溯身份，必须把它纳入 Lock 与 Fingerprint，那是破坏性变更（升 Save 版本 + 迁移）。

**当前实现状态**：`PackageManifest` 已具有 `contract / mechanism / content / presentation` 显式角色，严格 Authoring 模式会拒绝未声明角色、角色越界内容，以及 Mechanism Package 对非 Contract Package 的依赖。正式 Demo 0.5 由一个 Contract Package、三个互不依赖的 Mechanism Package 与一个 Content Package 实际承担并通过 Package Lock、Source Lock 和非法包门禁。Presentation 角色边界已经存在，但其资源格式与 Registry 尚未实现；`Dillen-Game/presentation/` 仍只是目录占位。

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

HOI3 导入不属于上述核心验收的前置条件。只有核心验收通过后，才恢复 HOI3 外部接入工作。

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
  Template / Algorithm / Data / GUI      e.g. HOI3 / TFH / Mods
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
              Schema / Algorithm / Definition / Spawn /
                    Capability / Resource Registries
                                     │
                                     ▼
                   Ruleset Composition + Integrity Gate
                                     │
                                     ▼
                            Runtime Compiler
                  Slot / Layout / Binding / Index / Schedule Plan
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
6. WorldBuilder 只消费 Frozen Runtime Catalog，不消费 HOI3 IR；
7. Kernel 对输入来源无感知。

### 2.2 关键中间产物

| 产物 | 所有者 | 内容 | 禁止事项 |
|---|---|---|---|
| Package Lock | Package Resolver | 确定的 Package 版本、依赖和顺序 | 不保存战局状态 |
| Native Package Source Lock | Source Pipeline | **每条 Source Artifact 一行**：Package Id、Package 版本、Source Layer 名、虚拟路径、内容指纹、字节长度（`kernel::SourceLockEntry`） | 不承担 Importer / Mapping 身份；不包含运行期对象地址 |
| External Projection Artifact Identity | Adapter | Corpus 快照摘要、Importer 版本与实现摘要、Normalized IR 摘要、Mapping Profile 版本与摘要，及其联合摘要（`adapter::ProjectionArtifactIdentity`） | 不进入 Native Source Lock；不直接创建 Runtime Instance |
| Ruleset Fingerprint | Runtime Compiler | 最终装配身份。**当前实现**只覆盖 Ruleset Definition + Package Lock + Native Source Lock（`ComputeRulesetFingerprint`）；**Projection Artifact Identity 尚未接入**，是 §3.18 已登记的缺口 | 不保存战局状态 |
| Parse Artifact | Parser | 语法结构、动态键、顺序和 Source Span | 不执行 Gameplay 行为 |
| Normalized External Source IR | Importer | 与目标 Ruleset 无关的规范化外部内容 | 不引用 Dillen Gameplay Target |
| Dillen Projection Artifact | Mapping Profile | 对目标 Contract 的声明式投影结果 | 不直接创建 Runtime Instance |
| Resolved Registry Set | Resolver / Registry | 已解析和验证的 Schema、Definition、Algorithm、Capability 与资源 | 不允许 Tick 期修改结构 |
| Frozen Runtime Catalog | Runtime Compiler | Slot、Layout、Binding、Index 与 Schedule Plan | 不保存当前战局状态 |
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
- 当前纯 Dillen（关闭 HOI3 Compatibility 与 Oracle）Windows x64 Debug / Release 测试均为 26 项且全部通过，Linux gcc / clang 配置为运行同一测试清单的阻塞门禁；`thread_contract_probe` 守住派发的顺序无关性；`dillen_demo_1_0_probe` 覆盖双外部机制包、真实 Package / Source Lock、可替换 Root、Query、Scheduled Event、RNG、Capability 跨机制调用和权威事务结果，`controlled_script_probe` 与 `projection_adapter_probe` 分别覆盖受控脚本和 Adapter 身份迁移，`mechanism_ids_probe` 冻结 Stable Identity 层的哈希输出，`capability_invocation_probe` 固化契约调用的解耦闭环，`scale_probe` 是代表性规模的正确性与耗时基线，`demo_0_5_vertical_slice_probe` 守住正式玩法闭环与 Package 门禁，`architecture_guard_probe` 把模块分层依赖与"无 HOI3/oracle include"变成源码级门禁。启用冻结 HOI3 Compatibility 后的旧兼容夹具仍引用整理前的仓库 Corpus 路径；按照当前冻结策略，它们将在未来 Adapter 恢复时改为测试显式传入的实际 Corpus Root，不在 Standalone 主线中临时回接旧路径。

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

纯 Dillen Demo 不要求立即复刻完整 HOI3 GUI。确定性 AI 属于 Simulation Algorithm Client；表现 AI 或外部辅助工具只能提交受验证 Command。未来窗口后端应复用 `StandaloneSession` 和 Query / Command 协议，不得绕过 Host 重新持有第二套世界状态。

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

纯 Dillen Demo 1.0 已通过。通用 `dillen::adapter` 基础层现已建立 Projection Artifact Identity 与 Adapter Migration：身份同时锁定 Corpus Snapshot、Importer 实现、Normalized IR Schema / Digest、Mapping Profile、目标 Root Ruleset 和生成 Source / Source Map 摘要；产物篡改会被拒绝，并可生成作为普通 Generated Source 进入 Package 的 Projection Lock Document。Migration Registry 只允许冻结后的显式身份迁移，要求 Corpus Snapshot 不变、每步输出重新封印并验证；无路径、歧义路径、转换拒绝和非法输出均独立诊断。该层不解析任何 HOI3 语义，也不绕过 Resolver 创建 Runtime 对象。

真实 External Corpus Importer / Mapping Profile 仍保持冻结；恢复时必须把 Projection Lock Document 作为普通 Source 纳入 Package / Source Lock，禁止把 Adapter 身份藏入 Kernel 或 Tick 热路径。

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
8. Standalone Host、GUI Contract 与端到端 Demo 测试；
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

**`Dillen-Game` 的当前状态（2026-08-31）——已经进入正式管线。** `contracts/demo_0_5`、`packages/economy`、`packages/technology`、`packages/production` 与 `content/demo_0_5` 均拥有显式角色 Manifest 和真实 SHA-256 `content_digest`，共形成 5 个锁定 Package、29 个 Source Lock 条目。原 `Project-Dillen/tests/fixtures/dillen_demo_0_5` 最小夹具已删除，前端黄金值和综合验收探针均直接读取 `Dillen-Game/`，因此不会再出现测试夹具与正式内容分叉。

`Dillen-Game/content/common` 中的 `.txt` 与无扩展名文件仍是未来游戏内容草稿，不属于 Demo 0.5 的五个 Source Layer，也不会进入其 Package Digest、Source Lock 或 Frozen Catalog。它们日后正式接入时仍必须先明确 Package 归属和授权格式；无扩展名文件若参与摘要，还需显式钉死跨平台行尾。

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
18. Windows x64 纯 Dillen 测试为 26 项；`thread_contract_probe`（派发顺序无关性）、`controlled_script_probe`、`projection_adapter_probe`、`mechanism_ids_probe`、`capability_invocation_probe`、`scale_probe`、`demo_0_5_vertical_slice_probe` 与 `architecture_guard_probe` 分别固化 Script 沙箱/持久化、Projection 身份/迁移、Stable Identity 冻结哈希、Capability 契约调用、代表性规模、正式玩法纵向切片，以及模块分层依赖与"无 HOI3/oracle include"的源码级门禁；
19. 纯 Dillen Demo 1.0 已以聚落增长与贸易周期两个外部机制包、均衡/加速两个可替换 Root Package / Source Layer 完成端到端验收，并固化闭包裁剪、源摘要篡改拒绝、存档恢复、确定性回放、Source Lock 篡改拒绝与跨 Root 读档拒绝。
20. Capability 级 **fire-and-forget 闭环**收口：`provides_capabilities` + `invoke_capability` + `payload / payload_from` + `capability_invoked` + `from_payload` + 定向单提供者（`target_role`）+ 显式版本协商（限定在 Package Lock 声明的提供集内）+ Controlled Script 与 Declarative 在运行期和加载期闭包上等价；跨机制交互不引用对方 Mechanism Type / Instance ID。Demo 0.5 已用生产报告、科研拨款和科技解锁三条真实 Capability 边完成闭环。**已确认的 v1 边界**：单向链路足够。同 Tick 多发送者写入同一接收实例的丢失更新**已于 2026-09-01 闭合**——不是靠改 ABI，而是靠 `MechanismAddFieldOperation`（内层 tag 7，纯加法）：命令携带增量而非绝对值，由 Executor 对已提交值做读改写，**已冻结的 v1 命令布局一个字节未动**。详见 §4.3 第二轮审查修复第 1 条。仍属后续纯加法能力的是：多 Operation、返回值、关联 ID。

**本轮已补齐的核心缺口**：

1. 受控 Script 的内存配额、指令边界可抢占沙箱、权威 Continuation 与持久化状态；受控 Script 现已能做全部通用事务（Query / Event / Command / Capability）+ `when` 条件，与 Declarative 后端共享编译下降形态与运行期执行；
2. Capability 调用 ABI 收口：定向单提供者、显式版本协商、Controlled Script 同等访问；
3. External Corpus Adapter 恢复所需的 Projection Artifact 联合身份、内容封印、Projection Lock 与 Adapter Migration。

**后续主线缺口**：

1. ~~**Capability 多发送者原子聚合未实现**~~——**已闭合（2026-09-01，见 §4.3 第二轮审查修复第 1 条）**：`MechanismAddFieldOperation`（内层 tag 7）纯加法追加，命令只携带增量，由 Executor 对已提交值做读改写。**已冻结的 v1 命令布局一个字节未动**。返回值与关联 ID 仍属 ABI v2。
2. **同相位算法并行未实现**——粗粒度 CoW 与快照共享载荷已完成（§3.20），派发的两相位结构与顺序守卫已完成（§3.9）；剩下的是 Worker Pool 本身，以及它必须同批带来的 1-vs-N 对拍探针和 Native Executor 的 `parallel_safe` 契约。
3. ~~**内层 variant tag 未逐项验证**~~——**已闭合（2026-08-31）**：`CheckFrozenCommandEncoding` 现对全部 19 个 tag 逐项断言（外层 11 个 `WorldCommandPayload` + 内层 8 个 `MechanismCommandOperation`）。判别力用注入验证过：读侧对调内层 tag 4/5，两者均无载荷，编码字节数与外层 tag 全部不变——正是旧检查必然放行的那一类——被精确捕获。
4. ~~**多步 Migration 无夹具**~~——**已闭合（2026-09-01）**。`CheckMigrationChain` 覆盖多步链、断链、版本倒退与同版本成环四种情况。顺带查明两件事：**版本倒退的步骤在注册期就被拒**（`Register` 的 `target.formatVersion < source.formatVersion` 校验），所以一整类环根本无法构造；**环检测是双重的**——visited 集合与应用步数上限，单独关掉任一道都仍会终止。
5. **加载期只有 Demo 0.5 基线**——正式 5 Package / 29 Source 的 Parse → Resolve → Compile → Freeze 已纳入 30 秒硬门禁并输出实测微秒值；更大规模 Package 图仍需独立基准。
6. External Corpus Adapter ABI、Normalized IR 容器与 Mapping Profile 执行器本身；
7. **Projection Artifact Identity 未接入 Ruleset Fingerprint**——`ComputeRulesetFingerprint` 目前只吃 Ruleset Definition + Package Lock + Native Source Lock，投影身份是独立的一套（§2.2），两者尚未合并。

**暂停项**：

- HOI3 Importer 新语义切片；
- HOI3 Mapping Profile；
- HOI3 Runtime WorldBuilder；
- Oracle 横向逆向扩展；
- 以 HOI3 War / Diplomacy 作为当前主线验收样本。

### 3.19 工程化加固与代码修复

本轮不改变任何 Kernel 边界、权威状态所有权、依赖方向或 Ruleset 语义；全部改动均已通过纯 Dillen 测试。

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
- Demo 1.0 两个机制包已删除 `query_at_least { type = dillen.demo1.<对方> }`，改为 `dillen.demo1.market_pressure` 契约。契约现在住在**中立的 `dillen.demo1.contracts_package`**（`packages/contracts/`，首个走通完整 Authoring 管线的 `.dcapability`），聚落包与贸易包**各自依赖它、互不依赖** —— 提供者实现从此可被替换。

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

## 4. 开发顺序与 Demo 计划

以下日期是目标窗口，不是允许绕过验收门禁的硬截止日。前一 Demo 未通过时，后一 Demo 自动顺延；不得通过把 HOI3 兼容代码临时并入 Kernel、跳过持久化或把程序化测试定义冒充外部 Package 来维持日期。

### 4.0 编号裁定（2026-08-31）

此前本章存在编号与时间线冲突：§4.1-8 宣称"Demo 1.0 已完成"，§4.2 同时把 Demo 0.2 作为当前冻结阶段，而 §4.5 又把 Demo 1.0 的目标日期留在 2027 年。三者不可能同时为真。

**裁定：保留现有编号，不重排；重新分类被误称为 Demo 1.0 的那个产物。**

`Project-Dillen/demo/dillen_demo_1_0/` 及 `dillen_demo_1_0_probe` 交付的是**Kernel 工程验证夹具（Kernel Verification Fixture）**，不是产品意义上的 Demo 1.0。它证明的是引擎能力——外部 Package 能定义机制、能装配、能运行、能存档、能确定性回放、能换 Root——用的是聚落增长与贸易周期两个**刻意最小化**的机制。它不包含任何真实玩法纵深。

据此：

| 里程碑 | 含义 | 状态 |
| --- | --- | --- |
| **Kernel 工程验证夹具** | 引擎能力的端到端证明（现 `dillen_demo_1_0`） | **已完成**，持续作为回归门禁 |
| **Demo 0.2 — Kernel Contract Freeze** | 让"冻结"成为可执行门禁 | **已完成**，标签 `demo-0.2-contract-freeze` |
| **Demo 0.5 — External Mechanism Vertical Slice** | 第一个**真实玩法**纵向切片：经济—科研—生产 | **已完成**，持续作为正式内容回归门禁 |
| **Demo 0.8 — Persistence and Replay** | Durability 闭环的产品级验收 | 当前后续主线；核心机制与 Demo 0.5 真实负载均已就绪 |
| **Demo 1.0 — Pure Dillen Standalone** | 产品级独立运行平台 | 未开始，目标窗口不变 |

**命名遗留（未处理）**：目录名 `demo/dillen_demo_1_0/` 与探针名 `dillen_demo_1_0_probe` 仍沿用旧称，与上表的"工程验证夹具"定位不符。改名涉及目录、CMake、探针注册与 Demo 说明文档，属独立的仓库整理工作，**尚未执行**；在改名前，凡本文出现 `dillen_demo_1_0` 一律按"Kernel 工程验证夹具"理解。

### 4.1 当前主线顺序

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
11. **主线冻结后再定义 External Corpus Adapter ABI（身份与迁移基础已完成）**：已用合成 Projection 固化 Corpus / Importer / IR / Mapping / Target / Generated Source 联合身份、篡改拒绝和唯一迁移链；下一步仍须先用合成 Corpus 实现 Importer / Mapping 执行分离与 Projection Lock 接线，再恢复 HOI3 工作。

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

范围七项逐条落实：Root/Extension 语义定稿见 §4.1-1；无 HOI3 依赖与架构诊断由 `architecture_guard_probe` 源码级门禁守住；最小外部 Package Fixture 见下文重新分类的工程验证夹具；World Transaction / Inbox / RNG / Scheduler / Snapshot 契约、线程契约、Capability fire-and-forget v1 三项冻结均已由下方"实际交付物"落为可执行门禁。四条验收：`dillen-standalone-windows-x64` Debug / Release 当前各 26/26；Linux gcc / clang 各 Debug / Release 配置为同一 26 项阻塞门禁；balanced/accelerated 可换 Root；稳定 Sequence 见 §4.1-6。

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

**（3）已冻结契约面清单 —— 已完成（2026-08-30，2026-08-31 扩充）**：`Project-Dillen/FROZEN_CONTRACTS.md` 按存档与回放格式 / 稳定身份 / Capability ABI v1 / 线程契约 / 模块分层 / Authoring DSL 六类列出冻结项，**每一项标明由什么守卫**。第 0 节写明变更规则（纯加法允许；破坏性变更需升版本 + 迁移 + 修订 §4.2 + 更新黄金值；禁止为了让构建变绿而重置黄金值），末节诚实列出**尚无守卫的缺口**：线程契约尚无运行期守卫（并行未实现，1-vs-N 对拍探针待补）、多步 Migration 无夹具、黄金值本身可被人为重置（只能靠评审纪律）。Authoring DSL 的 Parse / Resolve / Compile / Diagnostic 四面黄金锁定已经闭合；`CONTRIBUTING.md` 已指向该文件。

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
Worker Pool 与 1-vs-N 对拍（Demo 0.8 之后）；`-Werror`（构建问题，非架构问题，已搁置）。

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
# Dillen-Game/packages/economy/algorithms/budget.dalgorithm，错误写法
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
  | Diagnostic | `authoring_diagnostic_contract_probe` | **100** 个码 + 12 个端到端触发 |

  诊断码当前是 **100** 个：在既有语法与管线诊断上新增 `invoke_capability` 载荷互斥校验，以及 Package 角色、依赖角色和严格显式角色诊断。

  **硬性规则（三次踩坑后写下）：`EncodeInstruction` 每加一个 `case`，
  `tests/fixtures/dillen_dsl_v1/algorithms/coverage.dalgorithm` 必须同时加一条构造它的指令。**
  命令编码黄金同理（见 §4.2 命令编码黄金值条目）。
  没有被构造过的备选项等于没有被冻结——`AddField` 内层 tag 7、
  `SetComponentFieldComputed`、按角色寻址的两个操作码，三次都是加了编码分支而没加夹具构造，
  三次都是全套测试在黄金一字未动的情况下全绿。**这条规则靠记性守不住，只能靠 review 时对照。**

  执行顺序为：先锁现有构造（1617 字节）→ 加读操作数与聚合、黄金值必须不动 → 用真实经济—科研—生产替换草稿 → 扩充 fixture 至完整覆盖后重取（2377 字节）→ 追加 `invoke_capability payload_from` 与 Package 角色契约后重取（2449 字节）→ 角色读路径按 `reference_type` 解析、并追加 `cancel_events` 后重取（2462 字节）→ 追加计算式 `set_component_field` 后重取（2509 字节）→ 追加按角色寻址的两个 Component 写入操作码后重取（2582 字节）。旧常量载荷编码保持不变，只有使用动态载荷的新构造追加读路径编码，因此仍遵守纯加法冻结规则。

  **这个漏洞是 GCC 的 `-Wswitch` 报出来的，不是自查发现的**：黄金编码器的 switch 少了三个 enum 分支，冻结面正好在新功能处开洞。同一批告警还暴露出 `IsValidAlgorithmInstruction` 漏校验两个新指令种类、以及**受控脚本后端整个没接读操作数**（`lowerTransact` 与脚本侧条件下降是独立于声明式的另一份代码）。MSVC /W4 三条都不报（C4061/C4062 默认关闭）。这是 Linux 阻塞门禁在本轮的实际产出。

### 4.4 Demo 0.8：Persistence and Replay

**目标日期：2027-01-15**

范围：

- Save / Load Entity、Component、Relation、Mechanism、Clock、RNG、Inbox、Queue 和 Sequence；
- Package Lock、Ruleset Fingerprint 与 Schema / Algorithm 版本验证；
- 至少一条 Schema Migration；
- 固定 Command Log 的确定性 Replay。

验收：

- 存档前后权威 Checksum 一致；
- 读档后所有派生索引和 Snapshot 可重建；
- 不兼容 Ruleset 明确拒绝，不能静默读取；
- Replay 在重复运行中产生相同 Fact Stream 和最终状态。

**当前验收状态**：核心 Durability 闭环已由 `persistence_replay_probe` 达成；`standalone_host_probe` 已进一步覆盖 Platform 文件写入、同目录临时文件原子替换和恢复，文件路径仍不进入权威存档格式。

**Demo 级连续运行对拍（2026-09-01 提前落地）**：`demo_0_5_vertical_slice_probe` 的 `CheckSaveResumeEquivalence()` 已实现本 Demo 的主门禁形态——正式内容跑 Tick 1–20 直通，与 Tick 12 存档后载入全新 Session 再跑 13–20，要求两者 Tick 20 存档逐字节相同。它比“存档字节稳定”强一个量级：后者只证明编解码可往返，前者证明**存档没有漏掉任何影响后续演算的状态**。Demo 0.8 的剩余工作是把它推广到 Migration 之后的续跑，以及更长的 Tick 跨度。

### 4.5 Demo 1.0：Pure Dillen Standalone

**目标日期：2027-02-28**

核心目标：证明 Dillen 是不依赖 HOI3 Corpus、Importer、Mapping Profile、Oracle 或 `hoi3_tfh.exe` 的独立机制化运行平台。

内容：

- 一个可替换 Root Ruleset 契约及均衡/加速两个 Root 实现；
- 至少两个外部 Gameplay Mechanism Package；
- 一个原生 Dillen Content Package 和场景；
- 最小 Standalone Host / Inspector；
- Query / Command GUI 或 CLI 交互；
- Save / Load、Migration、Replay 和确定性 Checksum；
- 故障 Package、非法 Command 和超预算 Algorithm 的隔离演示。

最终门禁：

- 完全关闭并删除构建产物中的 HOI3 Adapter 与 Oracle，Demo 仍可运行；
- 新机制无 Kernel 业务特判和专用 `RuntimeXXXState`；
- 更换 Root Ruleset 可以改变机制组合；
- 运行期不依赖可编辑字符串结构；
- 核心验收标准 1—10 全部通过。

**当前验收状态**：`Project-Dillen/demo/dillen_demo_1_0` 已提供一个中立 Contracts Package、两个互不依赖的外部 Gameplay Package、两个可互换 Root Package / Source Layer、CLI 命令流与说明文档；`dillen_demo_1_0_probe` 已验证当前四 Package 与 18 个真实 Source Artifact 锁定、Root Fingerprint / Spawn 差异、未选择 Definition / Spawn / Algorithm 裁剪、三 Tick Query / Event / RNG / Transaction 固定结果、真实 Demo Save 恢复、双次确定性 Replay、Package 源摘要篡改拒绝、Source Lock 篡改拒绝和跨 Root 读档拒绝。Migration、非法 Command 与超预算隔离门禁继续由同一纯 Dillen 测试组中的既有 Probe 联合覆盖。

### 4.6 Demo 1.1：Authoring Hardening

**目标窗口：2027-03 至 2027-05**

范围：

- 更完整的错误恢复、Source Map 和诊断；
- Package 模板、Schema 文档生成和开发工具；
- 增量编译、脏索引和事务暂存优化；
- 跨平台数值固定向量和性能基线（构建与全部 probe 的跨平台一致性已于 2026-08-30 达成并转为 CI 阻塞门禁，见 §3.19；此项余下的是**浮点数值**跨平台固定向量与各平台性能基线）；
- Script GUI 的 Standalone Backend 评估。

### 4.7 External Corpus Demo：暂不排期

只有 Demo 1.0 通过后才允许启动，顺序固定为：

1. 用合成 Corpus 建立通用 Importer ABI 和 Normalized Source IR 版本契约；
2. 用两个不同 Mapping Profile 把同一合成 IR 投影到两个不同 Root Ruleset；
3. 验证 Importer 不引用目标 Contract、Mapping 不读取原始文件；
4. 再迁移现有 HOI3 Parser 原型为独立 HOI3 Importer；
5. 最后选择最小 HOI3 资源切片建立 Mapping Profile。

External Corpus Demo 的成功只证明来源规范化和声明式投影闭环，不自动宣称 HOI3 Gameplay 等价。

---

## 5. 当前阶段明确禁止的工作

- 继续堆叠 HOI3 Country、War、Diplomacy、Technology 等专用 Runtime State；
- 继续扩展 HOI3 Importer 语义切片；
- 在 Kernel 中增加 HOI3 专用 Query、Command、Capability 或 Setter；
- 让 Mapping Profile 解析 HOI3 原始文件；
- 让 Importer 引用 Dillen Gameplay Contract；
- 用 Oracle 逐 Tick 轨迹定义 Dillen Runtime；
- 在 Persistence 和外部 Mechanism 纵向管线完成前制作完整大战略玩法；
- 为未来可能使用的功能提前扩大 Kernel 公共 API。

当前唯一主线是：**先让纯 Dillen 的外部机制定义、装配、运行、查询、持久化和确定性回放形成完整闭环，再恢复任何外部 Corpus 兼容工作。**
