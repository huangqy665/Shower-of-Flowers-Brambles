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
| `set_component_field` | 写入指定 Entity 的 Component 字段 | Entity、Component 与字段必须存在且类型匹配 |
| `add_relation` | 在两个稳定 Entity 之间增加 Relation | 必须满足 Relation Schema |
| `spawn_mechanism` | 按 Spawn Definition 创建机制实例 | Spawn 必须进入 Frozen Catalog |
| `schedule_event` | 向 Algorithm Inbox 调度确定性事件 | 使用 Tick 偏移、优先级和标量 Payload |
| `create_rng` | 创建稳定 RNG Stream | Stream ID 必须有效且不能重复 |
| `advance_rng` | 按期望 Draw Count 推进 RNG Stream | Draw Count 不一致时事务拒绝 |

`set_field` 与 `add_field` 可附加 `when`，当前支持 `field_equals`、`query_at_least`、`scheduled_event` 和 `rng_modulo`。Query 可统计 Entity、Component、Relation 或 Mechanism Type；Event 与 RNG 条件只读取同代际 Snapshot。

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

Script v1 支持 `set_state`、`add_state`、`set_field`、`add_field`、`transition_lifecycle`、`jump`、`jump_if_state_equals`、`yield` 和 `halt`。跳转目标是当前阶段的零基指令下标，也可等于阶段长度表示完成。状态类型由初值固定；`script_memory_limit_bytes` 约束全部持久状态的确定性结构化占用。达到 `script_slice_instruction_budget` 或执行 `yield` 时，VM 在指令边界抢占，把状态与 Program Counter 通过同一 World Transaction 提交；二者进入 Save v4 和 Replay。内存越界会丢弃整次输出并按 Failure Policy 记录权威 Fault。

`native` 继续通过宿主显式注册的 Executor 执行。Declarative VM 与 Controlled Script VM 都在指令边界消费确定性预算；Native Executor 获得协作式 Budget Tracker。墙钟耗时只写入当次 `AlgorithmInvocationResult` 诊断报告，不进入 Authoritative World、Save、Replay Checksum 或 Failure Policy。Kernel 不会不安全地强杀任意 C++ 回调；真正的进程级卡死保护必须由非权威 Host Watchdog 提供。

## 4. Mechanism Definition

```text
mechanism_definition = {
    name = dillen.demo.default_counter
    mechanism = dillen.demo.counter
    schema_version = 1
    algorithm = dillen.demo.counter_algorithm
    algorithm_version = 1
    fields = {
        value = 5
    }
}
```

Definition 在 Resolver 的 Resolve 阶段绑定已经冻结的 Schema 与 Algorithm；未知字段、错误类型、缺失必填字段和悬空 Algorithm 会被拒绝。

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
- 受控 Script 已完成 `外部语法 → Slot 编译 → 确定性 VM → 指令边界抢占 → 权威状态/Continuation 事务 → Save/Replay` 基础闭环；Query、Event/Command 上下文和 Capability 访问仍待按可持久化 Frame 契约扩展。

纯 Dillen Demo 1.0 位于 `demo/dillen_demo_1_0`，展示两个独立机制包、真实包依赖和可替换 Root Ruleset。
