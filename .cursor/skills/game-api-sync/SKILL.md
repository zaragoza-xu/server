---
name: game-api-sync
description: >-
  根据飞书权威接口文档（ECS 快照）对齐当前仓库协议代码，或拉取 Wiki 元数据。
  在 client/server 仓库中说「对齐 XX 模块代码到文档」时使用。禁止本地 lark-cli、禁止 Generated 目录、禁止自动开 PR。
disable-model-invocation: false
---

# game-api-sync — 协议文档对齐

飞书 Wiki 为唯一权威源。文档快照由 ECS 中央服务提供，成员**不安装 lark-cli**。

## 前置条件

1. 已配置环境变量（PowerShell）：

```powershell
$env:API_SYNC_BASE = "http://120.27.249.20"
$env:API_SYNC_TOKEN = "ed7484c01552b1d3c271870a4c128bc7e1c0e5b92c732d33"
```

2. 当前 Git 分支是开发者**有意工作的分支**（不要切换分支、不要开 PR）。

## 对齐代码到文档（主流程）

当用户说「根据最新飞书接口文档，对齐本仓库【模块名】模块的协议代码」时：

1. **确认模块名**（如 `战斗`、`地图`）。可用：

```powershell
$h = @{ Authorization = "Bearer $env:API_SYNC_TOKEN" }
Invoke-RestMethod -Headers $h "$env:API_SYNC_BASE/api/snapshot/modules"
```

2. **拉取快照**（保存到临时文件便于阅读）：

```powershell
$module = "战斗"   # 替换为实际模块
$uri = "$env:API_SYNC_BASE/api/snapshot?module=$([uri]::EscapeDataString($module))"
Invoke-RestMethod -Headers $h $uri | ConvertTo-Json -Depth 20 | Out-File -Encoding utf8 "$env:TEMP\api-snapshot-$module.json"
```

3. **读路径映射**：打开本仓库根目录 `config/wiki-registry.yaml` 中对应模块的 `client_glob` 或 `server_glob`（若缺失，从中央仓 [game-api-sync](https://github.com/Pluto599/game-api-sync) 复制该文件到 `config/`）。

4. **定位现有源文件**：用 glob 搜索**已有**协议文件，**禁止**新建 `Generated/` 或平行目录。

5. **对照快照就地修改**：按 snapshot 中 `messages`、类型约束 struct/enum 修改字段、序列化、注释；列出将改文件清单，改完后由开发者自行 `git commit`。

6. **禁止**：本地 `lark-cli`、自动创建 PR、切换分支、写入 `Generated/`。

## 拉取 Wiki 节点列表（可选）

```powershell
Invoke-RestMethod -Headers @{ Authorization = "Bearer $env:API_SYNC_TOKEN" } "$env:API_SYNC_BASE/api/wiki-nodes"
```

## 权威文档链接

- 接口文档：https://my.feishu.cn/wiki/NYw0wSFwji6j3skwW4ocIrkxn6b
- 类型约束：https://my.feishu.cn/wiki/CF6owdEKLiYhwmkBrMxcgxK8nde
