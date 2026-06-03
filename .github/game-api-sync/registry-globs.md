# wiki-registry 路径（glob）

对齐/对比时：**glob 默认范围 + 用户 @/指定路径 + 必要时自行列目录搜索**，合并为最终文件集。

用户或 Agent 发现协议文件**不在 glob 中**：更新本仓 `config/wiki-registry.yaml` 的 `client_glob`/`server_glob`（优先显式路径列表），再改代码或发 compare。

细则与 `_status` 约定见中央仓 `.cursor/skills/game-api-sync/references/registry-globs.md`。
