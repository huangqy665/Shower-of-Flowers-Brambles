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
| **全部 11 种命令载荷 + 6 种 Mechanism 操作**的字段顺序与编码 | 同一探针 `CheckFrozenCommandEncoding()`（516 字节 / `5610142064737695594`）+ tag 往返 |
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
| 只有算法派发可并行；提交 / 稳定标识分配 / 事件与快照发布 / RNG 推进永远单线程 | 备忘录 §3.9；**当前实现为单线程，尚无并行实现，故无运行期守卫** |
| 线程数不影响权威输出（1 线程与 N 线程存档逐字节相同） | **待补** —— 实现并行时必须同时提供 1-vs-N 对拍探针 |

## 5. 模块分层

| 冻结项 | 守卫 |
| --- | --- |
| 8 个 Standalone 模块的依赖方向（kernel ← world ← runtime ← persistence 等） | `architecture_guard_probe` 源码级 include 闭包检查 |
| Standalone 源码不引用 HOI3 / oracle / compatibility | 同上 |

## 6. 已知缺口（本清单的诚实部分）

- **线程契约无运行期守卫** —— 契约已写下但尚未实现并行，1-vs-N 对拍探针待补。
- **Authoring DSL 语法面无黄金锁定** —— 现有 Authoring 探针验证行为，但没有
  "这段 DSL 恰好编译成这些字节码"的黄金值。DSL 是作者可见契约，值得补。
- **Migration 链只有一条测试路径** —— `persistence_replay_probe` 覆盖一次旧格式
  迁移；v5 之后的多步迁移尚无夹具。
- **黄金值可被人为重置** —— 没有任何机制能阻止有人直接改数字。这最终靠评审
  纪律，本文第 0 节的规则就是评审时的对照标准。
