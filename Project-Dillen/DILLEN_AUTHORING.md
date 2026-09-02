# Dillen 外部 Authoring 格式（纵向切片 v1）

本文描述当前已接入 `FileCatalog → Parser Registry → Resolver → Registry → Runtime Compiler → Frozen Runtime Catalog` 的 Dillen 原生外部格式。

## 1. 目录与扩展名

| 虚拟目录 | 扩展名 | 根语句 | 输出 |
| --- | --- | --- | --- |
| `packages/**` | `.dpackage` | `package_manifest` | `PackageManifestRegistry` |
| `capabilities/**` | `.dcapability` | `capability_contract` | `RuntimeCapabilityContractRegistry` |
| `components/**` | `.dcomponent` | `component_schema` | `ComponentSchemaRegistry` |
| `entities/**` | `.dentity` | `entity_definition` | `EntityDefinitionRegistry` |
| `relations/schemas/**` | `.drelation` | `relation_schema` | `RelationSchemaRegistry` |
| `relations/definitions/**` | `.drelationdef` | `relation_definition` | `RelationDefinitionRegistry` |
| `mechanisms/**` | `.dmechanism` | `mechanism_template` | `MechanismSchemaRegistry` |
| `algorithms/**` | `.dalgorithm` | `algorithm_descriptor` | `AlgorithmRegistry` |
| `definitions/**` | `.ddefinition` | `mechanism_definition` | `MechanismDefinitionRegistry` |
| `spawns/**` | `.dspawn` | `mechanism_spawn` | `MechanismSpawnDefinitionRegistry` |
| `rulesets/**` | `.druleset` | `root_ruleset` / `extension_ruleset` | `RulesetComposer`、`RulesetRegistry` |

路径可以继续包含子目录。文件类型由虚拟路径和扩展名确定，根语句与文件类型不匹配会产生错误。

### 1.1 Package 与 Source Layer 身份

每个参与 Authoring 的 Source Layer 必须包含且只能包含一个有效的 `.dpackage`。该 Manifest 是该层全部有效 Source Artifact 的唯一 Package Owner；同一层缺少 Manifest、存在多个 Manifest，或该 Package 未进入当前 Root 的 Package Lock，都会在 Runtime Compile 前被拒绝。

`content_digest` 不是任意占位字符串。Authoring Pipeline 会按虚拟路径稳定排序该层除 `.dpackage` 自身以外的全部有效 Source Artifact，以 `dillen.package.content.v1` 帧格式计算 SHA-256，并要求结果与 Manifest 中的 64 位小写十六进制摘要完全一致。Manifest 自身不进入 Package 摘要，以避免自引用；但 Manifest 文件仍进入 Source Lock，因此修改 Manifest 也会改变 Ruleset Fingerprint。

Source Lock 的每一项同时保存 `PackageId + PackageVersion + Source Layer + Virtual Path + Fingerprint + Size`。因此一个文件不仅被锁定内容，也被锁定到明确的 Package 版本。Root Ruleset 所在 Source Layer 同样必须拥有自己的 Package Manifest，并把该 Root Package 列入 `required_packages`。

## 2. Mechanism Template

```text
mechanism_template = {
    name = dillen.demo.counter
    version = 1
    fields = {
        field = {
            name = value
            kind = integer
            required = yes
            default = 0
            minimum_number = 0
        }
    }
    roles = {
        role = {
            name = owner
            reference_kind = entity
            minimum_count = 0
            maximum_count = 1
        }
    }
}
```

字段支持 `null`、`boolean`、`integer`、`decimal`、`string`、`reference`、`list` 和 `object` 类型声明。v1 外部初值只实现标量值；复杂值的结构化语法仍待后续补充。

## 3. Algorithm Descriptor

```text
algorithm_descriptor = {
    name = dillen.demo.counter_algorithm
    version = 1
    backend = declarative
    entry_points = { create tick command }
    deterministic = yes
    execution_policy = {
        instruction_budget = 4096
        script_slice_instruction_budget = 256
        script_memory_limit_bytes = 65536
        wall_clock_warning_microseconds = 50000
        failure_policy = fail_instance
    }
    program = {
        create = {
            transition_lifecycle = active
        }
        tick = {
            add_field = {
                field = value
                value = 1
            }
        }
        command = {
            set_field = {
                field = value
                value = 0
            }
        }
    }
}
```

`backend` 可声明为 `declarative`、`script` 或 `native`。`declarative` 必须提供 `program`，`script` 必须提供 `script`，且对应程序的阶段必须与 `entry_points` 完全一致。当前 Declarative 最小指令集如下：

| 指令 | 作用 | 约束 |
| --- | --- | --- |
| `set_field` | 把标量常量写入当前 Mechanism Instance 字段 | 值必须满足目标字段 Schema |
| `add_field` | 对当前整数或小数字段加常量 | 只允许数值字段；整数溢出和非有限小数会失败 |
| `transition_lifecycle` | 提交生命周期转换 | 必须满足 Kernel 生命周期转换规则 |
| `create_entity` | 按 Entity Definition 创建 Entity | Definition 必须已冻结 |
| `set_component_field` | 写入指定 Entity 的 Component 字段（常量或计算值） | Entity、Component 与字段必须存在；常量形式要求类型匹配，计算形式要求目标为数值字段 |
| `add_relation` | 在两个稳定 Entity 之间增加 Relation | 必须满足 Relation Schema |
| `spawn_mechanism` | 按 Spawn Definition 创建机制实例 | Spawn 必须进入 Frozen Catalog |
| `schedule_event` | 向 Algorithm Inbox 调度确定性事件 | 使用 Tick 偏移、优先级和标量 Payload；`delay` 必须为正 |
| `invoke_capability` | 向某 Capability Contract 的提供者发一次调用（默认广播，`target_role` 可定向单个） | 请求版本区间必须与组合 Ruleset 中某契约相交；`delay` 必须为正；不引用任何提供者 Entity / Instance ID |
| `create_rng` | 创建稳定 RNG Stream | Stream ID 必须有效且不能重复 |
| `advance_rng` | 按期望 Draw Count 推进 RNG Stream | Draw Count 不一致时事务拒绝 |

`invoke_capability` 是**单向、单 Operation、无返回值、无关联 ID** 的 fire-and-forget 调用 —— 即 **Capability ABI v1**，本节描述的语义在 Demo 0.2 定稿。契约的 `operations` 字段尚未接入调用点；将来的多 Operation / 返回值 / 关联 ID 属于 v2，会以纯加法方式引入，不改变本节语义。`invoke_capability = { capability = <契约名> payload = <标量> delay = <正整数> priority = <整数> target_role = <角色名> version = <正整数> }`（`target_role` 与 `version` 可选）。

- **版本协商**：`version` 请求一个精确契约版本；省略则接受任意版本。加载期 Runtime Compiler 只在 **Package Lock 声明为某锁定包所提供**的契约版本里区间内取最高；与之都不相交时编译报错（不静默取 latest，也不扫描整个 Registry）。定义契约的 Package 其 manifest 须用 `provides = { capability = { name = <契约名> version = <正整数> } }` 声明所提供的版本；一个 manifest 每个契约只能声明一个版本。提供端 `provides_capabilities` 同样解析为具体版本；只有版本相符的提供者会收到投递。
- **定向**：省略 `target_role` = 广播，运行期按稳定 Instance ID 顺序投递给每个 `provides_capabilities` 命中且版本相符的实例（零提供者是无害空操作）。`target_role` 命名调用方机制自身的一个角色 Slot，运行期从该 Slot 读出目标实例，只投递给它；该实例必须存在并提供相符版本，否则事务拒绝（定向未命中是作者错误）。角色 Slot 未绑定 mechanism 实例时算法 Fault。

`set_field` 与 `add_field` 可附加 `when`，当前支持 `field_equals`、`query_at_least`、`scheduled_event`、`rng_modulo` 和 `capability_invoked`。`capability_invoked = <契约名>` 是 `scheduled_event` 在 Capability 派生事件类型上的语法糖，用于提供者在 `event` 阶段识别一次 Capability 调用。Query 可统计 Entity、Component、Relation 或 Mechanism Type；Event 与 RNG 条件只读取同代际 Snapshot。

`set_field` / `add_field` 可用 `from_payload = yes` 代替 `value`，改用当前 Scheduled Event / Capability 调用的 Payload 作为操作数（仅在 `event` 阶段有效；`add_field` 要求数值字段）。这样提供者能读取消费者发送的数值，而不必知道消费者是谁。

### 读路径与计算式赋值

`set_field`、`add_field` 与 `set_component_field` 都可以用 `left` 代替 `value`，把写入的值改成一个
**读路径**求值的结果。`left` 与 `value` / `from_payload` 互斥。可选的 `op` 与 `right`
必须成对出现，构成一次二元运算：

```text
set_field = {
    field = base_income
    left = {
        role = capital
        relation = { type = dillen.demo.owns  direction = outgoing }
        component = dillen.demo.resources
        field = ore
        reduce = sum
    }
}

add_field = { field = balance  op = sub  left = { self_field = income }  right = { constant = 3.0 } }
```

读路径有且只有一个**根**：`constant`（字面量）、`event_payload`（当前事件载荷）、
`self_field`（本实例字段）或 `role`（本机制的一个角色 Slot）。`role` 根可选地经
`relation = { type = <关系名> direction = outgoing | incoming }` 走一跳，然后以
`component = <组件名> field = <字段名>` 读实体 Component，或以 `field = <字段名>` 读机制字段。
多个目标时用 `reduce` 归约：`require_one`、`sum`、`count`、`min`、`max`。
`op` 支持 `add`、`sub`、`mul`、`div`、`min`、`max`。

**算术全程走定点**：存储精度两位小数，内部精度四位，四舍五入远离零。中间结果不经过浮点，
溢出、除零与非有限值一律拒绝而不是静默产生 NaN。目标字段的**声明类型说了算**——把小数表达式写进
整数字段会舍入到整数，不会把字段类型改掉。

**`add_field` 的计算形式提交的是增量，不是绝对值**。这不是优化：绝对值是对派发期 Snapshot 算的，
同一阶段只要有两个以上调用写同一字段（Capability 多发送者汇聚正是这个形状），
它们会读到同一个陈旧基数并互相覆盖。增量没有基数。

**`set_component_field` 的计算形式提交绝对值**，因为 Component 字段目前没有 `add` 形式，
不存在同阶段汇聚。将来若增加，必须按同样的理由改成增量。

加载期由 Runtime Compiler 把字段名解析为 Definition 专属的 32 位 Slot，并生成无字符串、无循环的冻结字节码；运行期内建 VM 只读取当前 Instance，按顺序生成 `WorldTransaction`，不直接修改权威世界。空阶段合法，可用于声明当前阶段暂时无操作。

`execution_policy` 可省略并使用上例默认值。`instruction_budget` 必须大于零，是决定执行是否失败的确定性预算。`wall_clock_warning_microseconds` 只控制非权威运行诊断，设为 `0` 可关闭警告；它不会中止算法、丢弃事务、记录权威 Fault 或改变生命周期。旧字段 `timeout_microseconds` 暂作为同义兼容入口保留，但不再具有 Timeout 语义。Declarative 阶段若在加载期已超过指令预算，会直接拒绝注册或编译。`failure_policy` 支持：

| 值 | 失败后的权威状态 |
| --- | --- |
| `isolate_instance` | 保持当前生命周期，但隔离该实例的后续 Algorithm 调度 |
| `pause_instance` | Active 实例转为 Paused；Created 实例转为 Failed；同时隔离 |
| `fail_instance` | 非终态实例转为 Failed；同时隔离 |

每次故障会在 Mechanism Instance 中记录 Fault Code、阶段、次数和 Tick。恢复必须通过事务清除 Fault；若实例处于 Paused，还需在同一事务中显式恢复为 Active。

若 Descriptor 声明 `destroy`，进入 Completed 或 Failed 的非隔离实例会执行 Destroy 阶段；Destroy 输出与实例删除在同一事务提交。仍被其他 Mechanism Role 引用的实例不会删除，定向到已删除实例的待处理 Algorithm Event 会被原子取消。

受控 Script 使用 Dillen 自有的确定性字节码，不嵌入宿主 Lua。示例：

```text
algorithm_descriptor = {
    name = dillen.demo.controlled_script
    version = 1
    backend = script
    entry_points = { tick }
    execution_policy = {
        instruction_budget = 64
        script_slice_instruction_budget = 8
        script_memory_limit_bytes = 4096
    }
    script = {
        state = {
            iteration = 0
        }
        tick = {
            add_state = { state = iteration value = 1 }
            add_field = { field = value value = 1 }
            jump_if_state_equals = {
                state = iteration
                value = 10
                target_instruction = 4
            }
            yield = yes
            halt = yes
        }
    }
}
```

Script 的控制层指令：`set_state`、`add_state`、`jump`、`jump_if_state_equals`、`yield`、`halt`，以及不带 `when` 的 `set_field` / `add_field` / `transition_lifecycle`。跳转目标是当前阶段的零基指令下标，也可等于阶段长度表示完成。

除此之外，Script 阶段可直接写 **Declarative 后端的任意通用事务指令**——`create_entity`、`set_component_field`（含计算形式）、`add_relation`、`remove_relation`、`spawn_mechanism`、`schedule_event`、`cancel_event`、`create_rng`、`advance_rng`、`invoke_capability`——以及带 `when` 条件（`field_equals` / `query_at_least` / `scheduled_event` / `capability_invoked` / `rng_modulo`）、`from_payload = yes` 或 `left` 读路径的 `set_field` / `add_field`。它们与 Declarative 用同一套加载期下降和运行期执行（`EmitBytecodeTransaction`），语义完全一致，只是嵌在 Script 的控制流里。

状态类型由初值固定；`script_memory_limit_bytes` 约束全部持久状态的确定性结构化占用。达到 `script_slice_instruction_budget` 或执行 `yield` 时，VM 在指令边界抢占，把状态与 Program Counter 通过同一 World Transaction 提交；二者进入 Save 和 Replay。内存越界会丢弃整次输出并按 Failure Policy 记录权威 Fault。

`native` 继续通过宿主显式注册的 Executor 执行。Declarative VM 与 Controlled Script VM 都在指令边界消费确定性预算；Native Executor 获得协作式 Budget Tracker。墙钟耗时只写入当次 `AlgorithmInvocationResult` 诊断报告，不进入 Authoritative World、Save、Replay Checksum 或 Failure Policy。Kernel 不会不安全地强杀任意 C++ 回调；真正的进程级卡死保护必须由非权威 Host Watchdog 提供。

## 3.5 角色绑定

角色槽由 `.dmechanism` 的 `roles` 声明，由 `.ddefinition` 或 `.dspawn` 的 `roles` 填写。
两处语法相同，区别只有一个：**目标可以是什么**。

```text
# .ddefinition —— 只能指向 Entity
roles = {
    capital = { entity = { entity_type = dillen.demo05.country  definition = dillen.demo05.alvara } }
}

# .dspawn —— 还可以指向 Mechanism Instance
roles = {
    treasury = {
        mechanism_instance = {
            mechanism   = dillen.demo05.national_budget
            definition  = dillen.demo05.alvara_budget
            ordinal     = 0
        }
    }
}
```

Definition 是在任何实例存在之前写的，所以它只能命名 Entity —— Entity 的稳定 ID 由
`(entity_type, definition)` 直接导出。Spawn 是实例被创建的地方，实例 ID 由
`(mechanism, definition, ordinal)` 确定性导出，所以这里才是命名 Mechanism Instance
第一个有意义的位置。`ordinal` 可省略，默认 0，也就是 `count = 1` 的 Spawn 唯一产生的那个实例。

一个槽可以写多个目标（受 `maximum_count` 约束），`entity` 与 `mechanism_instance` 可以混写，
但必须与 Schema 声明的 `reference_kind` 一致，否则注册期拒绝。

`reference_kind = mechanism_instance` 的槽上，Schema 的 `minimum_count` 由
**Spawn 注册**强制，而不是 Definition 注册：Definition 层根本无法满足它，在那里要求它
只会让这一类 Schema 无法注册。约束没有放松，只是移到了唯一能满足它的地方。

绑定到不存在的实例不会被静默吞掉：读路径在运行期第一次求值时就报
`read path target Mechanism field is missing` 并按 Failure Policy 记录 Fault。

**运行期重绑定尚不存在**，见 `Project Dillen工程开发备忘录` C2 一节的理由。

## 3.6 批量表格：`entity_table` 与 `relation_table`

一个生成的世界是成千上万个同形状、且只会被整体重新生成的 Entity 与 Relation。
把它写成一文件一对象不是品味问题：Source Lock 会为每个文件留一条目，
每条目都被哈希进 Ruleset Fingerprint，Package 的 `content_digest` 要覆盖每个文件。
参考世界地图是 14187 个 Entity 和 41693 个 Relation——一文件一对象即 55880 条 Source Lock 记录。

```text
# entities/**/*.dentitytable
entity_table = {
    entity_type = dillen.map.region
    name_prefix  = dillen.map.region_
    component = {
        type = dillen.map.geography
        schema_version = 1
        columns = { source_id }        # 列名，顺序即行内取值顺序
    }
    rows = {
        row = { 1  1 }                 # 第一个值是名字后缀，其余按 columns 顺序
        row = { 2  2 }
    }
}

# relations/definitions/**/*.drelationtable
relation_table = {
    relation = dillen.map.borders
    schema_version = 1
    name_prefix = dillen.map.border_
    source_entity_type = dillen.map.region
    target_entity_type = dillen.map.region
    source_prefix = dillen.map.region_
    target_prefix = dillen.map.region_
    rows = {
        row = { 1_2  1  2 }            # 名字后缀、源后缀、目标后缀
    }
}
```

表格**不引入任何新的内核概念**：它产出的 `entity_definition` 与 `relation_definition`
和单条形式产出的完全相同，注册、校验、冻结路径也完全相同。
它属于 **Content** 包，和它折叠的单条形式一样。

行写成 `row = { ... }` 而不是扁平的值流，是为了让**坏行有自己的 span**——
表格通常是生成的，而生成内容恰恰是错误最难读的内容。
行内值的个数必须等于 `1 + 全部 columns 之和`，否则报 `table_row_arity`。

**Ruleset 侧**：一个 Ruleset 默认只选它点名的定义（闭包裁剪）。
点名 55880 个只是把庞大文件从内容挪进 Ruleset，所以有整体选择形式：

```text
required_entity_definitions   = { all = yes }
required_relation_definitions = { all = yes }
```

它是 opt-in 的，且**受 Package Lock 约束**——"全部"指已锁定的 Package 声明的全部，
不是磁盘上的全部。需要裁剪时照旧逐条 `requirement = { ... }`。

**生成内容的行尾**：`.dentitytable` 与 `.drelationtable` 已在 `.gitattributes` 中钉为 `eol=lf`。
`content_digest` 是对原始字节的 SHA-256，而生成内容没人逐行审阅——
CRLF 检出在 Package 摘要失败之前完全不可见。

## 4. Mechanism Definition

```text
mechanism_definition = {
    name = dillen.demo.default_counter
    mechanism = dillen.demo.counter
    schema_version = 1
    algorithm = dillen.demo.counter_algorithm
    algorithm_version = 1
    provides_capabilities = { dillen.demo.market_pressure }
    fields = {
        value = 5
    }
}
```

Definition 在 Resolver 的 Resolve 阶段绑定已经冻结的 Schema 与 Algorithm；未知字段、错误类型、缺失必填字段和悬空 Algorithm 会被拒绝。可选的 `provides_capabilities` 列出该 Definition 的实例响应的 Capability Contract；每项可以是裸契约名（接受 Ruleset 锁定的任意版本），也可以是 `requirement = { name = <契约名> minimum_version = <正整数> maximum_version = <正整数（不含）> }` 声明可接受的版本范围。Runtime Compiler 在其范围内解析为一个具体契约版本并纳入依赖闭包，无兼容版本时拒绝。

## 5. Mechanism Spawn

```text
mechanism_spawn = {
    name = dillen.demo.initial_counter
    mechanism = dillen.demo.counter
    definition = dillen.demo.default_counter
    count = 2
    fields = {
        value = 9
    }
}
```

Spawn 在 Definition 冻结后解析，可覆盖 Definition 的标量初值，但必须继续满足 Schema。

## 6. Root Ruleset

```text
root_ruleset = {
    name = dillen.demo.root
    version = 1
    allow_additions = { mechanism_schema mechanism_definition mechanism_spawn algorithm }
    required_schemas = {
        dillen.demo.counter = 1
    }
    required_algorithms = {
        dillen.demo.counter_algorithm = 1
    }
    required_definitions = {
        requirement = {
            mechanism = dillen.demo.counter
            name = dillen.demo.default_counter
        }
    }
    required_spawns = {
        requirement = {
            mechanism = dillen.demo.counter
            definition = dillen.demo.default_counter
            name = dillen.demo.initial_counter
        }
    }
}
```

`allow_additions` 使用 `package`、`mechanism_schema`、`component_schema`、`relation_schema`、`mechanism_definition`、`entity_definition`、`relation_definition`、`mechanism_spawn`、`algorithm` 或 `capability`。

## 7. Extension Ruleset

```text
extension_ruleset = {
    name = dillen.demo.audit_extension
    version = 1
    priority = 20
    target_root = dillen.demo.root
    target_minimum_version = 1
    target_maximum_version = 2
    required_algorithms = {
        dillen.demo.audit_algorithm = 1
    }
}
```

只有 Launch Selection 明确选择的 Extension 会参与组合。目标 Root、版本区间、允许类别和契约冲突均由 `RulesetComposer` 验证。

## 8. 当前边界

- 已完成 Package、Capability、Component、Entity、Relation、Mechanism、Algorithm、Definition、Spawn 与 Ruleset 进入 Registry 和 Frozen Runtime Catalog 的纵向闭环。
- 已支持 Root / Extension 外部定义、显式选择、确定性组合和 Fingerprint。
- Definition 与 Spawn 的外部初值当前只支持标量；结构化 List / Object / Reference 初值尚未接入。
- 多 Package Source Layer、Package Lock 与逐 Source Artifact 的真实 Source Lock 已进入 Frozen Catalog、Fingerprint 和 Persistence Identity。
- Runtime Compiler 只冻结 Root / Extension 组合所选择的依赖闭包；未选择的 Schema、Algorithm、Definition、Spawn 及其无关依赖可以存在于已加载 Registry，但不会进入 Frozen Catalog 或初始权威世界。
- Declarative Algorithm 已完成 `外部程序 → Query/Condition/通用事务解析 → Slot/Stable ID 编译 → Frozen Bytecode → 内建 VM → WorldTransaction` 的可执行闭环；当前指令集刻意保持无循环。
- Destroy、确定性指令预算、单实例 Fault 隔离、三种失败策略和显式恢复已接入权威事务与 Query Snapshot；墙钟阈值只产生非权威诊断。
- 受控 Script 已完成 `外部语法 → Slot 编译 → 确定性 VM → 指令边界抢占 → 权威状态/Continuation 事务 → Save/Replay` 闭环，并与 Declarative 后端完全对齐：全部通用事务指令 + `when` 条件经同一套下降与运行期执行（`EmitBytecodeTransaction`），只是可嵌在 Script 的跳转/yield 控制流里。
- Capability 调用：消费者只写契约名（可加 `version` 精确请求、`target_role` 定向单个提供者）；加载期把版本区间解析为具体版本，运行期按稳定顺序投递给版本相符的提供者。跨机制交互不引用对方的 Mechanism Type 或 Instance ID。

纯 Dillen Demo 1.0 位于 `demo/dillen_demo_1_0`，展示两个独立机制包、真实包依赖、可替换 Root Ruleset，以及两个机制**仅通过 `dillen.demo1.market_pressure` Capability Contract**交互（互不引用对方类型）。
