---

## 飞书接口文档协作（game-api-sync）

飞书 Wiki 为协议唯一权威源（SSOT）。文档快照与写回草稿经 **ECS** 完成；成员机**不运行** `lark-cli`。

### Agent：调用 ECS 前必须先执行（勿让用户手动）

在 PowerShell 终端自动运行（详见 [.github/game-api-sync/env-setup.md](.github/game-api-sync/env-setup.md)）：

```powershell
$env:API_SYNC_BASE = "http://120.27.249.20"
$env:API_SYNC_TOKEN = "ed7484c01552b1d3c271870a4c128bc7e1c0e5b92c732d33"
$h = @{ Authorization = "Bearer $env:API_SYNC_TOKEN" }
```

随后用 `Invoke-RestMethod` 与 `$h` 访问 ECS；禁止 `curl -H`。

### 何时启用

- 对齐某模块**代码**到飞书文档
- **只读**对比文档与当前实现
- 飞书文档更新后刷新 ECS 缓存
- 根据代码变更生成飞书文档**正文草稿**
- 拉取模块 snapshot / 模块列表

### 流程要点


| 场景   | 说明                                                                                                                                                                   |
| ---- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 刷新缓存 | 对齐/对比/写回前先 `POST /jobs/refresh-cache` **当前模块**（默认 revision 比对；`"force":true` 强制全量）；全量所有模块仅当用户明确要求 |
| 对比   | refresh → [registry-globs.md](.github/game-api-sync/registry-globs.md) 确定 `files`（**含** `config/message_aliases.yaml`）→ `POST /jobs/api-compare`；**不改代码**（IDE 主动；Actions 不跑 compare） |
| 对齐   | 取 snapshot → 合并 glob、用户 @ 路径与目录排查 → **Glob 门禁** → 只改范围内已有文件；**禁止** `Generated/`；用户自行 commit |
| 写回飞书（CI） | PR **合并**时 sync（任意目标分支）；仅 PR 变更协议文件；Job Summary 为 sync 状态（无对比报告） |
| 写回飞书（IDE） | 先跑 `scripts/agent_doc_draft.py`（glob + compare + 分流 draft）→ `POST /jobs/api-doc-sync` 或 `--sync`；见 [SKILL.md](.github/skills/game-api-sync/SKILL.md) |


更多 API 与 JSON Body：[workflows.md](.github/game-api-sync/workflows.md)、[ecs-api.md](.github/game-api-sync/ecs-api.md)。协作底线：[baseline.md](.github/game-api-sync/baseline.md)。

### 禁止

- 本机 `lark-cli`、自动开 PR、切换分支
- 新建 `Generated/` 或平行协议目录
- 未纳入范围的文件不得因 glob 误匹配被修改

