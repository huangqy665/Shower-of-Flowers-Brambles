# Pure Dillen Demo 1.0

该 Demo 不读取 HOI3 Corpus，不构建 Compatibility / Oracle Target，也不在 C++ Kernel
中注册任何 Gameplay 业务。两个机制完全由外部 Dillen Authoring 文件定义，Root Ruleset
通过独立 Source Layer 替换，**两个机制之间只通过一个 Capability Contract 交互，互不引用
对方的 Mechanism Type 或 Instance ID**。

## 1. 内容组成

| Source Layer | Package / Ruleset | 作用 |
| --- | --- | --- |
| `packages/contracts` | `dillen.demo1.contracts_package@1.0.0` | **中立契约包**：只声明 `dillen.demo1.market_pressure` Capability Contract，不含任何机制 |
| `packages/settlement_growth` | `dillen.demo1.settlement_growth_package@1.0.0` | 定义聚落增长机制（契约提供者），依赖契约包 |
| `packages/trade_cycle` | `dillen.demo1.trade_cycle_package@1.0.0` | 定义贸易周期机制（契约消费者），依赖契约包 |
| `rulesets/balanced` | `dillen.demo1.balanced_root_package@1.0.0` / `dillen.demo1.balanced_root@1` | 生成 2 个普通聚落和 1 个贸易周期（market_index 起始 10） |
| `rulesets/accelerated` | `dillen.demo1.accelerated_root_package@1.0.0` / `dillen.demo1.accelerated_root@1` | 替换 Root，生成 3 个高初值聚落和 1 个贸易周期（market_index 起始 30） |

两个 Root 使用相同的机制 Package，只替换场景 Spawn 和 Root Identity，因此会产生不同的
Source Lock、Ruleset Fingerprint 与初始权威世界。

## 2. 聚落增长机制（Capability 提供者）

Mechanism Template 有 `population`、`food_stock`、`label` 三个字段。Declarative Algorithm：

- `create`：激活实例；
- `tick`：`add_field food_stock 2` —— 聚落自行增长粮食，不依赖任何其他机制；
- `event`：`when = { capability_invoked = dillen.demo1.market_pressure }` 时
  `add_field { field = population from_payload = yes }` —— 按调用方发来的数值增长人口。

`default_settlement.ddefinition` 用 `provides_capabilities = { dillen.demo1.market_pressure }`
声明它响应该契约。聚落包**既不依赖也不引用**贸易包 —— 它只依赖中立契约包。

## 3. 贸易周期机制（Capability 消费者）

Mechanism Template 有 `market_index`、`completed_cycles`、`label`。Declarative Algorithm：

- `create`：激活，`create_rng { seed = 20260829 }`；
- `tick`：`add_field completed_cycles 1`；`add_field market_index 4`；
  `invoke_capability = { capability = dillen.demo1.market_pressure payload = 3 delay = 1 }`；
  `advance_rng count = 1`。

`invoke_capability` 只写契约名。运行期由 World Transaction Executor 按稳定 Instance ID
顺序，把这次调用投递给每个 `provides_capabilities` 里列出该契约、且版本相符的实例。贸易包
**既不依赖也不引用**聚落包 —— 它只依赖中立契约包。因此换一个同样提供
`dillen.demo1.market_pressure` 的包，就能替换掉聚落实现而不动贸易包一个字。

## 4. 三 Tick 权威结果（balanced）

- 每个聚落：`food_stock` 每 Tick +2 → 3 Tick 后 +6；`population` 收到两次
  `market_pressure` 投递（Tick 1 的调用 `delay = 1` 于 Tick 2 送达，Tick 2 的于 Tick 3
  送达）→ +6。
- 贸易周期：`market_index` 10 → 22，`completed_cycles` 0 → 3，RNG Draw Count 3。

`accelerated` 变体逻辑相同，只是初值不同（聚落 population 250 / food 90，market 30）。

## 5. 运行

在仓库根目录执行均衡 Root：

```powershell
& 'Project-Dillen\build-dillen-core\bin\Debug\project-dillen.exe' `
  --source 'contracts@0=Project-Dillen/demo/dillen_demo_1_0/packages/contracts' `
  --source 'settlement@10=Project-Dillen/demo/dillen_demo_1_0/packages/settlement_growth' `
  --source 'trade@20=Project-Dillen/demo/dillen_demo_1_0/packages/trade_cycle' `
  --source 'ruleset@100=Project-Dillen/demo/dillen_demo_1_0/rulesets/balanced' `
  --root 'dillen.demo1.balanced_root@1' `
  --commands 'Project-Dillen/demo/dillen_demo_1_0/commands/inspect_three_ticks.txt' `
  --no-prompt
```

把最后一个 `--source` 和 `--root` 换成 `rulesets/accelerated` /
`dillen.demo1.accelerated_root@1` 即可运行加速 Root。

聚落 Package 还包含一组完整但未被任何 Root 选择的 `unselected_probe` Template、
Algorithm、Definition 和 Spawn。它们会通过 Parser 与 Registry 校验，但 Runtime Compiler
的 Ruleset 依赖闭包会将其排除在 Frozen Catalog 和初始世界之外。

## 6. 验收

`dillen_demo_1_0_probe` 验证：

1. 四个 Package 与 18 个真实 Source Artifact 被 Package Lock 和绑定 Package 身份的
   Source Lock 锁定；
2. 两个 Root 均能独立启动，Fingerprint 不同，分别生成 3 / 4 个机制实例；
3. 已加载但未选择的 Template / Algorithm / Definition / Spawn 不进入 Frozen Catalog；
4. Package 源文件被篡改后，自动 SHA-256 `content_digest` 验证拒绝启动；
5. `invoke_capability` 的调用被投递给 `provides_capabilities` 匹配的聚落，`from_payload`
   把贸易周期发送的数值 3 应用到聚落人口；
6. 三 Tick 后聚落、市场和 RNG 权威状态符合固定结果；
7. 两个真实 Gameplay Package 完成 Save 恢复和双次确定性 Replay；篡改 Source Lock 与跨
   Root 读档均被拒绝；
8. 全程不依赖 HOI3 Compatibility 或 Oracle。

`capability_invocation_probe` 用合成的 pump / sink 机制单独固化契约调用的解耦闭环、按
payload 精确增量、零提供者时的无害空操作、版本协商的四类拒绝、Controlled Script 发起调用、
`target_role` 定向只投递给被点名的提供者，以及 Save v5 排队命令的往返一致。
