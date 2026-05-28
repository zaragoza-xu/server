# game-api-sync（VS Code / GitHub Copilot）

飞书 Wiki 为唯一权威源。用环境变量 `API_SYNC_BASE`、`API_SYNC_TOKEN` 访问 ECS 快照，**禁止**本机 `lark-cli`、**禁止**新建 `Generated/`、**禁止**自动开 PR。

## 对齐代码到文档

用户要求对齐某模块时：

1. 确认当前 Git 分支，不切换分支。
2. 用 PowerShell：`Invoke-RestMethod -Headers @{ Authorization = "Bearer $env:API_SYNC_TOKEN" } "$env:API_SYNC_BASE/api/snapshot?module=<模块名>"`
3. 读 `config/wiki-registry.yaml` 中本仓的 `client_glob` 或 `server_glob`，只改**已有**协议源文件。
4. 对照快照中的 struct/enum/messages 就地修改；列出修改文件，由用户自行 commit。

## 触发语示例

> 根据最新飞书接口文档，对齐本仓库【战斗】模块的协议代码

权威文档：<https://my.feishu.cn/wiki/NYw0wSFwji6j3skwW4ocIrkxn6b>、<https://my.feishu.cn/wiki/CF6owdEKLiYhwmkBrMxcgxK8nde>
