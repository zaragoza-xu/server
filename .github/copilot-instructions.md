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
| 刷新缓存 | `POST /jobs/refresh-cache`，Body 含 `module` 或 `{}` 全量                                                                                                                 |
| 对比   | 按 [registry-globs.md](.github/game-api-sync/registry-globs.md) 确定 `files` → `POST /jobs/api-compare`（按章节/方向/消息 + 字段类型）；**不改代码**                                                        |
| 对齐   | 取 snapshot → 合并 glob、用户 @ 路径与目录排查 → 漏网协议文件须**更新** `config/wiki-registry.yaml` → 只改范围内已有文件；**禁止** `Generated/`；用户自行 commit                                            |
| 写回飞书 | 模式 A：**h2**、**无 caption**、pre×2、**无【合并位置】**、无实例；ECS 插入 h1 客户端/服务端 分区末；见 [doc-write-format.md](.github/game-api-sync/doc-write-format.md) |


更多 API 与 JSON Body：[workflows.md](.github/game-api-sync/workflows.md)、[ecs-api.md](.github/game-api-sync/ecs-api.md)。协作底线：[baseline.md](.github/game-api-sync/baseline.md)。

### 禁止

- 本机 `lark-cli`、自动开 PR、切换分支
- 新建 `Generated/` 或平行协议目录
- 未纳入范围的文件不得因 glob 误匹配被修改

