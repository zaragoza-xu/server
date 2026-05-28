# API 协议协作规范

- 飞书 Wiki 为唯一权威源（见 `config/wiki-registry.yaml`）。
- 禁止在仓库中新建 `Generated/` 专用目录；直接修改现有协议相关源文件。
- 禁止在成员机器上运行 `lark-cli`；拉取文档元数据使用 `API_SYNC_BASE` + `API_SYNC_TOKEN`。
- 对齐代码前确认当前 Git 分支正确；自行 commit，不自动开 PR。

## 拉取 Wiki 节点列表

```powershell
Invoke-RestMethod -Headers @{ Authorization = "Bearer $env:API_SYNC_TOKEN" } "$env:API_SYNC_BASE/api/wiki-nodes"
```

## 对齐代码（在 Cursor 中说）

> 根据最新飞书接口文档，对齐本仓库【模块名】模块的协议代码。先读 config/wiki-registry.yaml 中的 glob，只改现有文件。
