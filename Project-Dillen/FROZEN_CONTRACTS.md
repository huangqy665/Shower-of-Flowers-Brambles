# 冻结契约面（Demo 0.2 Kernel Contract Freeze）

> **权威性**：本文列出的是 Demo 0.2 冻结的契约面。工程总裁决权仍在
> `Project Dillen工程开发备忘录.md`；本文是它 §4.2 的可检查展开，冲突时以备忘录为准。

冻结不是"以后别改"，而是：**改动这些东西必须是显式决定，并留下痕迹。**
每一项都注明由什么守卫。**没有守卫的项已诚实标出** —— 那是下一步该补的地方，
不是可以随便改的地方。

## 0. 变更规则

1. **纯加法**：新增指令、新增命令 tag（追加到 variant 末尾）、新增 Save 版本，
   都不破坏已冻结语义 —— 允许，只需补上对应守卫。
2. **破坏性变更**：改字段顺序/编码、重排 variant、改语义 —— 需要
   **(a)** 升 `kCurrentRuntimeSaveFormatVersion`、**(b)** 提供 Migration、
   **(c)** 修订备忘录 §4.2 并记录理由、**(d)** 更新对应黄金值。
3. **禁止**：因为某个守卫报错就直接改黄金值/断言让它通过。守卫报错时先回答
   "这是意外还是有意"，答案是"意外"就修代码。

## 1. 存档与回放格式

| 冻结项 | 守卫 |
| --- | --- |
| `kCurrentRuntimeSaveFormatVersion = 5` | `runtime_save_image.hpp` 常量；黄金字节间接锁定 |
| `MechanismCommandOperation` 7 个备选项的**位置** | `runtime_save_codec.cpp` 逐项 `static_assert` + `variant_size_v` |
| `WorldCommandPayload` 11 个备选项的**位置** | 同上 |
| `WorldEventPayload` 18 个备选项的**位置** | 同上（这些字节从不读回，但是 Replay Checksum 的输入） |
| 规范世界的存档**字节数与校验和** | `persistence_replay_probe` 黄金值（688 字节 / `7194244525752032699`） |
| 规范回放的 `finalStateChecksum` / `factStreamChecksum` | 同一探针黄金值 |
| **全部 11 种命令载荷 + 全部 7 种 Mechanism 操作**的字段顺序与编码（1 种在第一条事务、其余 6 种在第二条） | 同一探针 `CheckFrozenCommandEncoding()`（516 字节 / `5610142064737695594`）+ **全部 18 个 tag 逐项对位**（外层 11 个 `WorldCommandPayload`，内层 7 个 `MechanismCommandOperation`） |
| 上述全部值**跨平台一致** | Windows MSVC / Linux GCC / Linux Clang 三平台 CI 阻塞门禁 |
| Authoring 源文件的 `content_digest` 稳定性 | `.gitattributes` 对全部 `.d*` 扩展名固定 `eol=lf` |

## 2. 稳定身份

| 冻结项 | 守卫 |
| --- | --- |
| 全部 `Stable*Id` 的哈希函数、归一化规则、哈希域字符串 | `mechanism_ids_probe` 冻结十六进制期望值 |
| `StrongId` 的内存布局（`sizeof` / `alignof` / trivially-copyable / standard-layout） | `mechanism_ids.hpp` 内 21 条 `static_assert` |
| Ruleset Fingerprint 的计算输入与顺序 | `runtime_catalog_probe` + 存档黄金值 |

## 3. Capability 调用 ABI v1（fire-and-forget）

| 冻结项 | 守卫 |
| --- | --- |
| `invoke_capability` 的 DSL 形状（`capability delay priority payload [target_role] [version]`） | `capability_invocation_probe` + `authoring_pipeline_probe` |
| `provides_capabilities` 的两种写法（裸名 / `requirement` 范围块） | 同上 |
| 加载期版本协商：只在 Package Lock 提供集内解析，不相交即编译拒绝 | `capability_invocation_probe` 三个拒绝用例 |
| 一个契约身份只能由一个锁定 Package 提供 | `RejectAmbiguousCapabilityProviders` + 探针用例 |
| 广播 / `target_role` 定向投递语义 | `capability_invocation_probe` 端到端用例（含判别力验证） |
| `InvokeCapabilityCommand` 的字段与 Save v5 布局 | 上表命令编码黄金值 |

**不在冻结范围**：`RuntimeCapabilityContract::operations`。多 Operation / 返回值 /
关联 ID 属于 **Capability ABI v2**，将以纯加法引入。

## 4. 线程契约

| 冻结项 | 守卫 |
| --- | --- |
| 只有算法派发可并行；提交 / 稳定标识分配 / 事件与快照发布 / RNG 推进永远单线程 | 备忘录 §3.9；派发本身已改为两相位（枚举序建计划 → 按计划下标填槽位），`thread_contract_probe` 守住"填槽顺序不影响任何权威字节" |
| Query 快照面：Entity / Component / Relation / Mechanism / **Scheduled Event Inbox**（2026-09-01 新增，只读） | 各子快照按值持有 CoW 载荷，发布是引用计数递增 |
| 派发结果写入由枚举位置决定的槽位，与完成顺序无关 | `thread_contract_probe`：整个世界跑两遍（正序填槽 / 逆序填槽），存档镜像与 Fact Stream 必须逐字节相同。Event 相位两条投递路径均覆盖（自定向 + 宿主广播扇出），最大计划 144 项 |
| 线程数不影响权威输出（1 线程与 N 线程存档逐字节相同） | **仍待补** —— 上一条证明的是顺序无关性，不是并发执行下的内存安全；实现 worker pool 时必须同时提供 1-vs-N 对拍探针 |

## 5. Authoring DSL —— 作者可见契约

外部 Package 作者写的是这一层。它与存档格式有一处**不对称，且不利于作者**：存档格式变了有版本升级 + 迁移可走，**DSL 变了没有迁移机制**，已写的内容只能手改。所以这四面按可观察结果分别冻结。

| 面 | 冻结项 | 守卫 |
| --- | --- | --- |
| **Parse** | 哪些文件被认领、分类成什么格式、同虚拟路径由哪一层胜出 | `authoring_frontend_golden_probe`：3912 字节 / 校验和 `3730319217720541124` |
| **Resolve** | Package Lock 与 Source Lock 的锁定身份 | 同上：3137 字节 / 校验和 `14470188716694633576` |
| **Compile** | 字节码与 Slot 布局 | `authoring_compile_golden_probe`：2462 字节 / 校验和 `16428176961995718018` |
| **Diagnostic** | 97 个 `dillen.authoring.*` 稳定码 | `authoring_diagnostic_contract_probe`：源码级注册表比对 + 9 个端到端触发 |
| Slot 按**字段名排序**分配，源码顺序不是契约 | 作者可自由重排字段而不影响编译产物 | Compile 黄金值（换序不动，改名即动） |
| 授权扩展名的 `eol=lf` | `content_digest` 是原始字节的 SHA-256，行尾漂移即身份漂移 | `.gitattributes` 覆盖全部 11 个扩展名；Parse 黄金值编码文件大小，CRLF 回归会被抓 |

**加法性规则**：新增指令、操作数来源、归约、运算符一律只能追加，**不得改变既有构造编译出的字节**。`invoke_capability payload_from` 以可选尾段加入黄金编码：旧的常量载荷编码保持原样，只有新构造追加读路径编码；完整覆盖夹具当前锁定为 2462 / `16428176961995718018`。

## 6. 模块分层

| 冻结项 | 守卫 |
| --- | --- |
| 8 个 Standalone 模块的依赖方向（kernel ← world ← runtime ← persistence 等） | `architecture_guard_probe` 源码级 include 闭包检查 |
| Standalone 源码不引用 HOI3 / oracle / compatibility | 同上 |

## 7. 已知缺口（本清单的诚实部分）

- **线程契约只守住了一半** —— 顺序无关性已由 `thread_contract_probe` 守住（逆序填槽必须产出逐字节相同的存档与 Fact Stream）。**并发执行下的内存安全仍无守卫**，1-vs-N 对拍探针必须与 worker pool 同批落地。
  另需记录：冻结当时契约描述的结构**并不存在** —— 派发在过滤循环里用 `push_back` 追加结果，执行顺序与槽位顺序是焊死的，根本无法并行。结构已补齐（两相位），这条留在这里是为了提醒：冻结一个从未执行过的契约，就是冻结一个假设。
- ~~**Authoring DSL 语法面无黄金锁定**~~ —— **已闭合（2026-08-31）**，见第 5 节：Parse / Resolve / Compile / Diagnostic 四面均已锁定。
- **DSL 定点标度只冻了存储侧** —— 存储标度 10⁻²（两位小数）是冻结契约；表达式内部标度 10⁻⁴ 不进存档、不进 Query、不进 Package，可随时调整。改**存储**标度会改掉所有既有存档。
- **`role → Mechanism 字段` 的读取已可正确解析，但角色仍无法绑定** —— 编译期现按角色声明的 `reference_type` 解析目标 layout（未声明即拒绝；此前静默按**调用方自身** layout 解析，字段同名即读到错误槽位）。但 `.ddefinition` 的角色绑定只支持 Entity 引用，`.dspawn` 无 `roles` 键，`MechanismCommandOperation` 也无 SetRole——**`mechanism_instance` 角色槽在当前引擎里没有任何写入路径，永远为空**。补齐需要新增 Mechanism 操作（追加 variant 备选项 + 升 Save 版本），属独立决策。
- ~~**`cancel_event` 无 DSL 语法**~~ —— **已闭合（2026-09-01），但不是按序列号闭合的**。按序列号取消永远不会有语法：序列号在 `AlgorithmInbox::Schedule()` 即提交期分配，VM 发出命令时它还不存在；把它写回字段需要给 `ScheduledEventScheduleCommand` 加目标槽位，那是存档格式变更，且暴露的是内部标识而非作者语义。
  改为把 **Inbox 暴露为 Query 快照面**，新增 `cancel_events = { type = X }`，运行期展开成 N 条既有的 `CancelEvent` 命令。**无新命令、无新 variant 备选项、Save 仍是 v5**；按 N 计费；只能取消本实例自己的事件。
- ~~**内层 variant tag 未逐项对位**~~ —— **已闭合（2026-08-31）**：`CheckFrozenCommandEncoding` 现对全部 18 个 tag 逐项断言。判别力已用注入验证：读侧对调内层 tag 4/5（两者均无载荷，字节数与外层 tag 全不变——正是旧检查漏掉的那一类），被精确捕获。
- ~~**Migration 链只有一条测试路径**~~ —— **已闭合（2026-09-01）**：`CheckMigrationChain` 覆盖两步链走通（值 1→12→123，顺序错即数值不同）、断链拒绝为 `PathMissing`、版本倒退步骤在**注册期**即被拒、同版本指纹成环被检出。环检测实为**双重**：visited 集合 + 应用步数上限，单独关掉任一道仍会终止。
- **黄金值可被人为重置** —— 没有任何机制能阻止有人直接改数字。这最终靠评审
  纪律，本文第 0 节的规则就是评审时的对照标准。
