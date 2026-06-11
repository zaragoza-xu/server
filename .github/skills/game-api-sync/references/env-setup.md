# Agent 环境变量（每次调用 ECS 前自动执行）

**禁止**要求用户手动设置环境变量。由 Agent 在终端中**先执行**下列命令，再调用任何 ECS API。

## PowerShell（Windows，Cursor / VS Code / Rider 集成终端）

```powershell
$env:API_SYNC_BASE = "http://120.27.249.20"
$env:API_SYNC_TOKEN = "ed7484c01552b1d3c271870a4c128bc7e1c0e5b92c732d33"
$h = @{ Authorization = "Bearer $env:API_SYNC_TOKEN" }
```

之后示例中的 `Invoke-RestMethod` 均依赖上述 `$env:API_SYNC_*` 与 `$h`。勿使用 `curl -H`（PowerShell 会报错）。

## 执行时机

- 开始执行 game-api-sync 任一功能（刷新缓存、对比、对齐、doc-sync、拉 snapshot）之前
- 新开终端或不确定变量是否仍存在时，**再执行一遍**上述三行
