# API 协议协作规范（VS Code / Copilot）

- 飞书 Wiki 为唯一权威源；模块与代码路径见 `config/wiki-registry.yaml`
- 禁止在成员机器运行 `lark-cli`；通过 `API_SYNC_BASE`、`API_SYNC_TOKEN` 访问 ECS
- 禁止新建 `Generated/` 或平行协议目录；只修改已有源文件
- 对齐前确认当前 Git 分支；自行 review 后 commit，不自动开 PR、不自动切分支

Agent 调用 ECS 前须在终端执行环境变量，见同目录 `env-setup.md`（勿让用户手动设置）。

对齐/对比：glob 与用户要求、目录排查并用；协议文件不在 glob 中时更新 `wiki-registry.yaml`（见 `registry-globs.md`）。

## 触发语示例

- 根据最新飞书接口文档，对齐本仓库【模块名】模块的协议代码
- 对比【模块名】模块飞书文档与当前仓库实现的差异，生成对比报告并指出实现缺陷
- 请刷新 ECS 上【模块名】模块的接口文档缓存
- 根据当前代码变更，生成飞书文档更新草稿
