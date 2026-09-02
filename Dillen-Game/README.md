# Dillen-Game 文件树

`Dillen-Game` 是正式 Authoring Source。目录按**内容领域**组织，Package 角色仍保持严格隔离。

## 规则

1. 一级目录表达领域，不产生加载语义；`.dpackage`、Source 配置和 Root Ruleset 才决定装配。
2. 每个 Source Layer 必须且只能拥有一个 Package Manifest。
3. Contract、Mechanism、Content、Presentation 不得因为物理位置相邻而混入同一 Source Layer。
4. 每个文件必须具有唯一 Package 所有者；未封装草稿不会被隐式加载。
5. `common/` 不是全局命名空间。正式使用其中定义的 Package 必须显式声明依赖。
6. Presentation 不进入权威 Ruleset Fingerprint；改变 Gameplay Meaning 的内容不得伪装为 Presentation。

## 当前目录

| 路径 | 职责 |
| --- | --- |
| `common/` | 尚未封装的共享静态定义草稿；未来应拆入明确 Contract 或 Content Package |
| `demo_0_5/contracts/` | Demo 0.5 共享 Contract Package |
| `demo_0_5/content/` | Demo 0.5 Entity、Relation、Definition、Spawn 与 Root Ruleset |
| `economy/demo_0_5/` | Demo 0.5 经济 Mechanism Package |
| `economy/demo_0_5_rebalanced/` | Demo 0.5 可替换经济 Mechanism Package |
| `technology/demo_0_5/` | Demo 0.5 科研 Mechanism Package |
| `production/demo_0_5/` | Demo 0.5 生产 Mechanism Package |
| `map/source/` | 外部地图语料；只供离线 Importer 使用，不进入运行期 |
| `map/contracts/` | 地图 Entity/Relation/Capability Contract Package |
| `map/world/` | 由地图语料生成的权威 Content Package 与 Root Ruleset |
| `production/map_world/` | 地图规模生产机制验证 Package |
| `presentation/map_world/` | 地图窗口、字体副本、索引纹理和绑定声明 |
| `presentation/fonts/` | 生成 Presentation Package 时使用的原始字体资源 |
| `events/`、`decisions/`、`history/` | 后续独立内容包入口，目前仍为草稿 |
| `rulesets/` | 可替换 Root/Extension Ruleset 的正式入口 |

## 新增机制

新增机制应优先建立领域目录，例如：

```text
economy/
├─ contracts/          # 可选，独立 Contract Source Layer
├─ official/           # 一个 Mechanism Source Layer
│  ├─ packages/
│  ├─ mechanisms/
│  └─ algorithms/
└─ scenario_content/   # 可选，独立 Content Source Layer
   ├─ packages/
   ├─ definitions/
   └─ spawns/
```

以上三个目录若同时启用，必须分别注册为 Source Layer，不能把 `economy/` 整体注册为一个 Source Layer。
