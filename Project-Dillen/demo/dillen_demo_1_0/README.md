# Pure Dillen Demo 1.0

该 Demo 不读取 HOI3 Corpus，不构建 Compatibility / Oracle Target，也不在 C++ Kernel 中注册任何 Gameplay 业务。两个机制完全由外部 Dillen Authoring 文件定义，Root Ruleset 通过独立 Source Layer 替换。

## 1. 内容组成

| Source Layer | Package / Ruleset | 作用 |
| --- | --- | --- |
| `packages/settlement_growth` | `dillen.demo1.settlement_growth_package@1.0.0` | 定义聚落增长机制 |
| `packages/trade_cycle` | `dillen.demo1.trade_cycle_package@1.0.0` | 定义贸易周期机制，并依赖聚落增长包 |
| `rulesets/balanced` | `dillen.demo1.balanced_root_package@1.0.0` / `dillen.demo1.balanced_root@1` | 生成 2 个普通聚落和 1 个贸易周期 |
| `rulesets/accelerated` | `dillen.demo1.accelerated_root_package@1.0.0` / `dillen.demo1.accelerated_root@1` | 替换 Root，生成 3 个高初值聚落和 1 个贸易周期 |

两个 Root 使用相同的机制 Package，只替换场景 Spawn 和 Root Identity，因此会产生不同的 Source Lock、Ruleset Fingerprint 与初始权威世界。

## 2. 聚落增长机制

Mechanism Template：

```text
mechanism_template = {
    name = dillen.demo1.settlement_growth
    version = 1
    fields = {
        field = { name = population kind = integer default = 0 }
        field = { name = food_stock kind = integer default = 0 }
        field = { name = label kind = string default = "settlement" }
    }
}
```

Declarative Algorithm 在 Create 阶段激活实例；Tick 阶段通过 `query_at_least` 查询贸易周期机制是否存在。条件成立时，每 Tick 增加 3 人口和 2 粮食：

```text
add_field = {
    field = population
    value = 3
    when = {
        query_at_least = {
            kind = mechanism
            type = dillen.demo1.trade_cycle
            count = 1
        }
    }
}
```

## 3. 贸易周期机制

Mechanism Template 包含 `market_index`、`completed_cycles` 和 `label`。其 Declarative Algorithm 演示：

- Create 阶段创建稳定 RNG Stream；
- Create 阶段调度下一 Tick 的 `market_review`；
- Tick 阶段使用 Mechanism Query 与 `rng_modulo` 条件增加市场指数；
- 每 Tick 通过事务推进 RNG Draw Count；
- Event 阶段收到 `market_review` 后把市场指数设为 50；
- 三个 Tick 后市场指数为 58、周期数为 3、RNG Draw Count 为 3。

核心模板：

```text
create_rng = {
    stream = dillen.demo1.trade_rng
    seed = 20260829
}
schedule_event = {
    type = dillen.demo1.market_review
    delay = 1
    priority = 20
    payload = "scheduled market review"
}
add_field = {
    field = market_index
    value = 4
    when = {
        query_at_least = {
            kind = mechanism
            type = dillen.demo1.settlement_growth
            count = 1
        }
        rng_modulo = {
            stream = dillen.demo1.trade_rng
            modulo = 1
            equals = 0
        }
    }
}
```

## 4. Definition、Spawn 与 Root

机制包使用 `mechanism_definition` 绑定 Template、Algorithm 和默认字段：

```text
mechanism_definition = {
    name = dillen.demo1.default_settlement
    mechanism = dillen.demo1.settlement_growth
    schema_version = 1
    algorithm = dillen.demo1.settlement_growth_algorithm
    algorithm_version = 1
    fields = { population = 100 food_stock = 40 }
}
```

Root Source Layer 使用 `mechanism_spawn` 定义场景实例数量和初值，并在 `root_ruleset` 中锁定两个 Gameplay Package、当前 Root Package、Schema、Algorithm、Definition 与 Spawn：

```text
root_ruleset = {
    name = dillen.demo1.balanced_root
    version = 1
    required_packages = { ... }
    required_schemas = {
        dillen.demo1.settlement_growth = 1
        dillen.demo1.trade_cycle = 1
    }
    required_algorithms = { ... }
    required_definitions = { ... }
    required_spawns = { ... }
}
```

`balanced` 与 `accelerated` 目录分别提供一个 Root Source Layer。运行时只装载其中一个，实现 Root Ruleset 替换，不需要重新编译引擎。

聚落 Package 还包含一组完整但未被任何 Root 选择的 `unselected_probe` Template、Algorithm、Definition 和 Spawn。它们会通过 Parser 与 Registry 校验，但 Runtime Compiler 的 Ruleset 依赖闭包会将其排除在 Frozen Catalog 和初始世界之外。

## 5. 运行

在仓库根目录执行均衡 Root：

```powershell
& 'Project-Dillen\build-dillen-core\bin\Debug\project-dillen.exe' `
  --source 'settlement@0=Project-Dillen/demo/dillen_demo_1_0/packages/settlement_growth' `
  --source 'trade@10=Project-Dillen/demo/dillen_demo_1_0/packages/trade_cycle' `
  --source 'ruleset@100=Project-Dillen/demo/dillen_demo_1_0/rulesets/balanced' `
  --root 'dillen.demo1.balanced_root@1' `
  --commands 'Project-Dillen/demo/dillen_demo_1_0/commands/inspect_three_ticks.txt' `
  --no-prompt
```

将最后一个 Source 和 Root 替换为以下内容即可运行加速 Root：

```text
Project-Dillen/demo/dillen_demo_1_0/rulesets/accelerated
dillen.demo1.accelerated_root@1
```

## 6. 验收

`dillen_demo_1_0_probe` 验证：

1. 两个 Gameplay Package 与一个当前 Root Package 被 Package Lock 锁定，16 个实际 Source Artifact 被绑定 Package 身份的 Source Lock 锁定；
2. 两个 Root 均能独立启动，且 Fingerprint 不同；
3. 均衡 Root 生成 3 个机制实例，加速 Root 生成 4 个；
4. 已加载但未选择的 Template、Algorithm、Definition 与 Spawn 不进入 Frozen Catalog；
5. Package 源文件被篡改后，自动 SHA-256 `content_digest` 验证拒绝启动；
6. Query 条件、Scheduled Event、RNG 与通用 World Transaction 正常执行；
7. 三 Tick 后聚落、市场和 RNG 权威状态符合固定结果；
8. 两个真实 Gameplay Package 完成 Save 恢复和双次确定性 Replay；篡改 Source Lock 与跨 Root 读档均被拒绝；
9. 全程不依赖 HOI3 Compatibility 或 Oracle。

运行验收：

```powershell
& 'C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe' `
  --test-dir Project-Dillen/build-dillen-core -C Debug `
  -R '^dillen_demo_1_0_probe$' --output-on-failure
```
