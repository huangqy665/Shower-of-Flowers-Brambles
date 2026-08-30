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

#### 1.2.2 Dillen Mechanism Package 与 Gameplay Library

Mechanism Package 定义具体 Gameplay Domain 所需的：

- Entity / Component / Relation Type；
- Mechanism Template、Schema、Definition 与 Spawn Definition；
- Algorithm、Capability Contract、Query Contract 与 Command Contract；
- GUI Contract、本地化键、资源引用和 Migration。

Project Dillen 可以提供一套 **Reference Gameplay Library** 作为官方示例和默认发行内容，但该 Library 不是 Kernel，也不是所有 Ruleset 的强制基础。删除 Reference Gameplay Library 后，Kernel 仍必须能够装载其他完全不同的 Gameplay Package。

#### 1.2.3 Root Ruleset、Extension Ruleset 与 Content Package

每次启动必须明确选择一个 **Root Ruleset**。Root Ruleset 声明本次 Simulation 的最低 Gameplay Contract、必需 Package、允许的扩展点、覆盖策略和入口场景。

Root Ruleset 不是 Kernel 内部不可替换的“唯一 Core Ruleset”。发行版可以提供受保护的官方 Root Ruleset；Mod 可以在该 Root Ruleset 允许的范围内加载 Extension Ruleset。需要彻底改变玩法时，作者可以提供新的 Root Ruleset，而不是被迫修改 Kernel 或绕过一个全局不可修改的规则集。

Content Package 提供具体世界、场景、Definition、Spawn、历史、GUI、本地化和资源。Ruleset 决定装配哪些 Package；Package 自身不拥有运行时调度权。

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
| Source Lock | Source Pipeline | Corpus 摘要、Importer 版本、Normalized IR 摘要、Mapping Profile 版本与投影摘要 | 不包含运行期对象地址 |
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

**受控 Script 当前实现**：`script` Backend 已启用 Dillen 自有的确定性 Controlled Script Bytecode VM，不嵌入宿主 Lua、操作系统线程状态或不可审计的第三方 VM 堆。外部 `.dalgorithm` 可声明类型稳定的持久状态、`set/add_state`、`set/add_field`、生命周期转换、绝对跳转、条件跳转、`yield` 和 `halt`。Runtime Compiler 在加载期把状态名和字段名冻结为 Slot；VM 在指令边界按 `script_slice_instruction_budget` 抢占，将 Program Counter 与状态值作为 Mechanism Instance 权威状态，通过同一 World Transaction 原子提交。Create / Tick / Destroy 在对应阶段继续执行；被抢占的 Event / Command 帧在后续 Tick 优先恢复，当前最小语言不读取未持久化的宿主事件对象。

Script 状态使用确定性的结构化字节占用模型执行 `script_memory_limit_bytes` 配额；超额会丢弃整次输出并产生 `ScriptMemoryQuotaExceeded` 权威 Fault。状态值、每阶段 Continuation、Fault 与所有相关 Sequence 已进入 Save Format v4、候选世界验证和 Replay；v3 Codec 读取继续保留，以便显式 Runtime Migration。`controlled_script_probe` 已覆盖外部语法解析、加载时编译、自动抢占、跨 Tick 恢复、Save / Load 后续执行一致性与内存配额拒绝。

**下一缺口**：

- Capability 的实际调用 ABI，而不仅是版本绑定；
- Controlled Script 对 Query、Event Payload、Command View 与 Capability 的受控只读访问；任何扩展仍必须先定义可持久化 Invocation Frame，禁止直接保存宿主对象或指针。

### 3.9 Scheduler、Transaction、Event、Query 与 RNG

**当前实现**：

- World Transaction 已统一暂存 Entity、Component、Relation、Mechanism、Scheduled Event 和 RNG Store；
- 任一命令失败时不留下跨 Store 半更新；
- Command Queue 按 `notBeforeTick → priority → sequence` 稳定排序；
- Algorithm Inbox 按 `dueTick → priority → sequence` 稳定排序；
- Committed Fact Stream 与权威 Scheduled Inbox 已分离；
- RNG Stream 使用稳定 ID、Seed、Draw Count 和 Expected Draw Count；
- `WorldQuerySnapshot` 在单次发布中深拷贝 Entity、Component、Relation 与 Mechanism 四个权威 Store，并统一绑定同一 `Publication / Tick / Revision`；
- Entity 查询支持 Stable ID、Definition 和 Type 索引；Component 查询支持 Owner / Type、字段 Slot、按类型查 Owner 和按 Entity 查 Component Type；Relation 查询支持 Stable ID、Type、Outgoing 和 Incoming 索引；Mechanism 查询支持 Stable ID、Definition、Type、字段 Slot 和角色 Slot；
- `WorldQueryService` 通过不可变 `shared_ptr<const WorldQuerySnapshot>` 发布快照；调用方可跨后续 Tick 持有旧代际，旧快照不会被原地覆写；
- `KernelRuntime::AcquireQuerySnapshot()` 是 GUI、AI、工具和未来 Standalone Host 的稳定查询入口，`KernelRuntime::Query()` 用于当前调用栈内即时读取；原 `Snapshot()` 暂保留为 Mechanism 子视图兼容入口；
- Algorithm Runtime 已接收完整一致 Query Snapshot；算法可读取四类通用世界对象，但仍只能通过 Command / Transaction 修改权威世界；
- RNG Snapshot 与 World Query Snapshot 在每次 Runtime 发布时使用相同 Tick / Revision；
- Scheduler 的物理归属已从 Kernel 契约层迁入 `src/runtime`，由 `KernelRuntime` 持有并编排 Tick；Kernel 只保留可复用的状态、事务和编译契约；
- 当前纯 Dillen（关闭 HOI3 Compatibility 与 Oracle）Windows x64 测试为 16 项；`dillen_demo_1_0_probe` 覆盖双外部机制包、真实 Package / Source Lock、可替换 Root、Query、Scheduled Event、RNG 和权威事务结果，`controlled_script_probe` 与 `projection_adapter_probe` 分别覆盖受控脚本和 Adapter 身份迁移，`mechanism_ids_probe` 冻结 Stable Identity 层的哈希输出。启用冻结 HOI3 Compatibility 后的旧兼容夹具仍引用整理前的仓库 Corpus 路径；按照当前冻结策略，它们将在未来 Adapter 恢复时改为测试显式传入的实际 Corpus Root，不在 Standalone 主线中临时回接旧路径。

**下一缺口**：

- 脏索引、结构共享和增量快照优化；当前实现优先保证不可变性与跨 Store 一致性；
- 阶段级预算、明确 Scheduler Phase Contract；
- 更细粒度事务暂存，替换当前正确性优先的全 Store Copy；
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

- `RuntimeSaveImage` 当前格式版本为 3，保存 Active Ruleset、Extension 列表、Ruleset Fingerprint、完整 Package Lock，以及逐个记录 Package ID / Version、Source Layer、虚拟路径、内容 Fingerprint 和字节长度的真实 Source Lock；
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
| 计划中的 `adapters/hoi3/importer` | 无 | 独立 HOI3 Source Normalizer |
| 计划中的 `mappings/hoi3/*` | 无 | 独立 Mapping Profile 与 Projection 测试 |

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
14. 受控 Script 已完成外部语法、加载时 Slot 编译、确定性字节码 VM、指令边界抢占、权威状态/Continuation 事务提交、内存配额、Save v4 与 Replay 契约；
15. Destroy、确定性指令预算、非权威墙钟诊断、单实例 Fault 隔离、三种失败策略和显式恢复；
16. 全权威状态 Canonical Save / Load、原子恢复、显式 Schema Migration、固定 Command Log Replay 与稳定 Checksum；
17. 最小 Standalone Host、外部 Authoring Session 启动、交互/脚本化 CLI Inspector、即时与排队 Command、状态查询及原子 Save / Load 文件闭环；
18. Windows x64 纯 Dillen 测试已增至 16 项；新增 `controlled_script_probe`、`projection_adapter_probe` 与 `mechanism_ids_probe`，分别固化 Script 沙箱/持久化、Projection 身份/迁移，以及 Stable Identity 层的冻结哈希、归一化等价与运算符语义；旧兼容夹具的 Corpus 路径问题继续隔离为未来 Adapter 恢复工作，不计入当前 Standalone 主线验收；
19. 纯 Dillen Demo 1.0 已以聚落增长与贸易周期两个外部机制包、均衡/加速两个可替换 Root Package / Source Layer 完成端到端验收，并固化闭包裁剪、源摘要篡改拒绝、存档恢复、确定性回放、Source Lock 篡改拒绝与跨 Root 读档拒绝。

**本轮已补齐的核心缺口**：

1. 受控 Script 的内存配额、指令边界可抢占沙箱、权威 Continuation 与持久化状态；
2. External Corpus Adapter 恢复所需的 Projection Artifact 联合身份、内容封印、Projection Lock 与 Adapter Migration。

**后续主线缺口**：

1. Capability 的实际调用 ABI及 Controlled Script 的受控 Query / Event / Command / Capability 访问；
2. External Corpus Adapter ABI、Normalized IR 容器与 Mapping Profile 执行器本身；
3. Projection Lock Source 与正式 Package / Source Lock、Ruleset Fingerprint 的端到端接线。

**暂停项**：

- HOI3 Importer 新语义切片；
- HOI3 Mapping Profile；
- HOI3 Runtime WorldBuilder；
- Oracle 横向逆向扩展；
- 以 HOI3 War / Diplomacy 作为当前主线验收样本。

### 3.19 工程化加固与代码修复

本轮不改变任何 Kernel 边界、权威状态所有权、依赖方向或 Ruleset 语义；全部改动均已通过 16 项纯 Dillen 测试。

**构建与工程基线**：

- MSVC 构建启用 `/W4 /permissive-`；`std::visit` + `if constexpr` visitor 尾部 `return` 的 C4702 误报以 `/wd4702` 定点关闭并注释说明。当前标准核心在此配置下 0 warning。
- `cmake/DillenTargets.cmake` 增加非 MSVC（GCC / Clang）分支：`-Wall -Wextra -Wpedantic`。跨平台移植尚未完成（见 §4.6），该分支用于让 Linux CI 尽早暴露一致性与可移植性差距。
- 新增 `.github/workflows/ci.yml`：Windows MSVC 跑 `dillen-standalone-windows-x64` preset 的 configure / build / ctest；另有一个标注 `continue-on-error` 的 Linux（gcc/clang）job，可见但不阻塞。
- 仓库卫生：新增 `.gitignore` / `.editorconfig`，停止跟踪约 1790 个构建产物、`.vs/` 与本地日志；新增 `Project-Dillen/README.md`、根 `CONTRIBUTING.md`。引擎子树（`Project-Dillen/`）以 MIT License 发布（`Project-Dillen/LICENSE`）。

**正确性与健壮性修复**：

- 事务执行器调度事件时，`std::find_if` 结果先判 `end()`，找不到即拒绝事务，消除可能的迭代器越界解引用。
- Authoring 的 `Declare` / `Resolve` 与 `ValidateAndCompile` 一致，按 `CatalogDisposition::Active` 过滤 —— 落败的 Replace-Path 工件不再进入 Declare / Resolve。
- Declarative 程序中目标 Entity 无法解析的 `set_component_field`，在编译期与 `create_entity` / `spawn_mechanism` 一样硬失败，而非静默跳过。
- `schedule_event` 的 `delay == 0` 在加载期即拒绝（运行期必然被 `dueTick <= currentTick` 拒绝）。
- Save Codec 的 `String()` / `Raw()` 长度检查前置 `offset_ > limit_` 守卫，即使内部不变量被破坏也返回失败而非越界读。
- Runtime Compiler 编译 Spawn 时对 `FindLayout` 结果补 `nullptr` 检查，与 Definition / Entity-Component 路径一致。

**Stable Identity 层重构**：

- 21 个手写 ID / Slot 类型（各含 `struct` + `operator bool` + `== / != / <`）收敛为单个 `StrongId<Tag, Underlying, Empty>` 模板加 21 行 `using` 别名。类型仍是聚合体，`Id{}` / `Id{rawValue}` / `return {rawValue};` / 公开可变的 `.value` 全部不变。
- 21 条 `static_assert` 锁定每个 ID 的 `sizeof == 底层类型`、`alignof` 一致、trivially-copyable、standard-layout。
- 新增可选的 `std::hash<StrongId<…>>` 偏特化，不改动任何现有容器，仅为将来把热点有序查找替换为哈希查找解除障碍。
- 所有 `Stable*Id` 哈希函数、归一化规则与哈希域字符串一字未动；`mechanism_ids_probe` 以冻结的十六进制期望值锁定全部 17 个 `Stable*Id` 输出，`persistence_replay_probe` / `dillen_demo_1_0_probe` / `runtime_catalog_probe` 验证 Ruleset Fingerprint、存档字节与 Replay Checksum 逐字节不变。
- 新增 `mechanism_ids.natvis` 供 VS 调试器显示裸 ID 值。

新增一个 ID 类型现在是 1 行 `using` + 1 行 `static_assert`，不再需要修改多处 Kernel 样板。

---

## 4. 开发顺序与 Demo 计划

以下日期是目标窗口，不是允许绕过验收门禁的硬截止日。前一 Demo 未通过时，后一 Demo 自动顺延；不得通过把 HOI3 兼容代码临时并入 Kernel、跳过持久化或把程序化测试定义冒充外部 Package 来维持日期。

### 4.1 当前主线顺序

1. **Root Ruleset 收口（已完成）**：已移除全局不可替换 Core Ruleset 假设，完成显式 Root 选择、纯加法 Extension Composition、保护策略、确定性排序和 Fingerprint；后续 Override 只能在独立授权模型完成后增量加入。
2. **外部 Authoring 纵向管线（基础闭环已完成）**：Package Manifest、Capability Contract、Component Schema、Entity Definition、Relation Schema / Definition、Mechanism Template、Algorithm Descriptor、Mechanism Definition、Spawn、Root Ruleset 和 Extension Ruleset 已能从多个 Source Layer 进入 Registry；每层严格绑定唯一 Package，真实 Source 自动计算并验证 Package SHA-256 摘要，绑定 Package 身份的 Source Lock 已进入编译、Fingerprint 与持久化身份。只有组合 Root 的依赖闭包进入 Frozen Catalog。复杂值和面向作者的工具链作为后续增量能力补齐。
3. **通用 Query 完整化（已完成）**：已发布 Entity / Component / Relation / Mechanism 同代际不可变快照、稳定索引和跨发布代际安全句柄；增量快照属于后续性能优化，不再阻塞 Query 核心契约。
4. **可执行 Algorithm 后端（Declarative 与 Controlled Script 基础闭环已完成）**：外部 Declarative Program 已能编译 Query、字段、Scheduled Event 与 RNG 条件，以及 Entity / Component / Relation / Mechanism / Event / RNG 通用事务指令为 Definition 专属 Slot / Stable ID Bytecode，并由内建无循环 VM 生成 World Transaction；Controlled Script 已提供类型稳定的持久状态、跳转、条件、`yield/halt`、确定性切片抢占、内存配额和 Save / Replay 状态契约。Native 后端继续受显式 Executor Registry 与协作 Budget 约束。
5. **生命周期和 Fault 收口（已完成）**：Destroy、正数确定性指令预算、单实例权威 Fault 隔离、`isolate / pause / fail` 策略、显式恢复、引用保护和定向 Inbox 清理均已接入统一事务；墙钟阈值已从权威确定性结果中剥离，只产生 Invocation 诊断。Native C++ 回调不执行不安全的线程强杀，未来 Host Watchdog 或可抢占 Worker 也不得把墙钟结果回写为 Gameplay 状态。
6. **Persistence / Migration / Replay（已完成）**：已保存四类权威 Store、算法状态、Clock、RNG、Inbox、Queue、创建序号和稳定 Sequence；完成 Canonical Binary Codec、身份/版本拒绝、候选世界原子恢复、显式 Schema Migration、派生索引重建与固定 Command Log 的双次确定性回放。
7. **Standalone Host（已完成）**：已提供纯 Dillen `project-dillen` CLI、外部 Authoring Session 启动、Query 状态检查、即时/排队 Command、Tick 驱动、脚本化命令流和原子 Save / Load 文件闭环；窗口后端属于后续 Platform 增量，不再阻塞 Host 核心契约。
8. **纯 Dillen Demo 1.0（已完成）**：聚落增长与贸易周期两个外部机制包已通过 Package Lock 和真实 Source Lock 进入 Standalone；均衡/加速 Root 各自作为正式 Root Package，可在不重新编译引擎的情况下替换，并产生不同 Fingerprint、Spawn 组合和权威初始状态。独立 Probe 已固化 Query、Scheduled Event、RNG、通用事务、Ruleset 闭包裁剪、Package 源摘要篡改拒绝、Save 恢复、双次 Replay、Source Lock 篡改拒绝与跨 Root 读档拒绝。
9. **主线冻结后再定义 External Corpus Adapter ABI（身份与迁移基础已完成）**：已用合成 Projection 固化 Corpus / Importer / IR / Mapping / Target / Generated Source 联合身份、篡改拒绝和唯一迁移链；下一步仍须先用合成 Corpus 实现 Importer / Mapping 执行分离与 Projection Lock 接线，再恢复 HOI3 工作。

### 4.2 Demo 0.2：Kernel Contract Freeze

**目标日期：2026-09-30**

范围：

- Root Ruleset / Extension Ruleset 语义定稿；
- Kernel、World、Runtime、Parser Target 不依赖 HOI3 Adapter；
- World Transaction、Inbox、RNG、Scheduler 和 Snapshot 契约冻结；
- 形成最小外部 Package Fixture；
- 架构诊断能够阻止非法依赖和缺失 Contract。

验收：

- `DILLEN_BUILD_HOI3_COMPATIBILITY=OFF` 时 Standalone 构建和核心测试通过；
- Kernel 公共头文件不包含 HOI3 类型；
- Root Ruleset 可以被另一份测试 Root Ruleset 替换；
- 同优先级 Command / Event 使用稳定 Sequence。

### 4.3 Demo 0.5：External Mechanism Vertical Slice

**目标日期：2026-11-15**

范围：

- 外部文本定义一个 Kernel 事先不知道的 Entity、Component、Relation 和 Mechanism；
- 外部 Definition / Spawn 创建实例；
- 最小 Declarative / Bytecode Algorithm 响应 Create、Tick、Event、Command；
- 两个机制通过公开 Capability 和 Transaction 交互；
- CLI Inspector 通过 Query 读取状态并提交 Command。

验收：

- 不修改 Kernel C++ 即可替换其中一个机制包；
- 删除一个可选机制包不会破坏另一机制和 Kernel；
- 非法 Package 在 WorldBuilder 前被拒绝；
- 相同输入和 Seed 连续运行产生一致 Checksum。

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

**当前验收状态**：`Project-Dillen/demo/dillen_demo_1_0` 已提供两个独立外部 Gameplay Package、两个可互换 Root Package / Source Layer、CLI 命令流与说明文档；`dillen_demo_1_0_probe` 已验证当前三 Package 与 16 个真实 Source Artifact 锁定、Root Fingerprint / Spawn 差异、未选择 Definition / Spawn / Algorithm 裁剪、三 Tick Query / Event / RNG / Transaction 固定结果、真实 Demo Save 恢复、双次确定性 Replay、Package 源摘要篡改拒绝、Source Lock 篡改拒绝和跨 Root 读档拒绝。Migration、非法 Command 与超预算隔离门禁继续由同一纯 Dillen 测试组中的既有 Probe 联合覆盖。

### 4.6 Demo 1.1：Authoring Hardening

**目标窗口：2027-03 至 2027-05**

范围：

- 更完整的错误恢复、Source Map 和诊断；
- Package 模板、Schema 文档生成和开发工具；
- 增量编译、脏索引和事务暂存优化；
- 跨平台数值固定向量和性能基线；
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
