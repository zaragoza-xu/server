# ECS API 与环境变量

## Agent 必须先执行（每次调用 ECS 前，勿让用户手动）

在 **PowerShell 终端**由 Agent 自动运行：

```powershell
$env:API_SYNC_BASE = "http://120.27.249.20"
$env:API_SYNC_TOKEN = "ed7484c01552b1d3c271870a4c128bc7e1c0e5b92c732d33"
$h = @{ Authorization = "Bearer $env:API_SYNC_TOKEN" }
```

## 常用接口

| 方法 | 路径 | 说明 |
|------|------|------|
| GET | `/health` | 无需 Token |
| GET | `/api/snapshot/modules` | 已有快照模块列表 |
| GET | `/api/snapshot?module=战斗` | 模块快照 JSON |
| POST | `/jobs/refresh-cache` | Body：`{"module":"战斗"}`；可选 `"force":true` 跳过 revision 比对 |
| POST | `/jobs/api-compare` | Body：`module`、`repo`、`files`（**须含** `config/message_aliases.yaml`）；可选 `target`、`scoped` |
| POST | `/jobs/api-doc-sync` | Body：`module`、`repo`、`summary`、`files_changed`、`docx_draft`、`target` |
| POST | `/jobs/module-system-doc-sync` | Body：`module`、`repo`、`mode`、`files_changed`、`docx_draft`（creator profile 写模块系统设计 wiki） |
| GET | `/api/status` | 各模块 `cached_at`、`fetched_at`、`api_docs`/`type_constraints` revision |

## 示例

```powershell
Invoke-RestMethod "$env:API_SYNC_BASE/health"
Invoke-RestMethod -Headers $h "$env:API_SYNC_BASE/api/snapshot/modules"

$module = "战斗"
$uri = "$env:API_SYNC_BASE/api/snapshot?module=$([uri]::EscapeDataString($module))"
Invoke-RestMethod -Headers $h $uri | ConvertTo-Json -Depth 20 | Out-File -Encoding utf8 "$env:TEMP\api-snapshot-$module.json"

Invoke-RestMethod -Method Post -Headers $h -ContentType "application/json" `
  -Body '{"module":"战斗"}' "$env:API_SYNC_BASE/jobs/refresh-cache"

# 飞书刚改完 / 强制全量拉取
Invoke-RestMethod -Method Post -Headers $h -ContentType "application/json" `
  -Body '{"module":"战斗","force":true}' "$env:API_SYNC_BASE/jobs/refresh-cache"
```

## 权威飞书文档

- 接口文档：https://my.feishu.cn/wiki/NYw0wSFwji6j3skwW4ocIrkxn6b
- 类型约束：https://my.feishu.cn/wiki/CF6owdEKLiYhwmkBrMxcgxK8nde
