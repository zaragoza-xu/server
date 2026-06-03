---
name: game-api-sync
description: >-
  飞书权威接口文档与 ECS 快照：刷新缓存、对比文档与代码差异、对齐协议代码、写回飞书正文草稿（标题标记待审查）。
  在 client/server 仓库处理协议对齐、api-sync、wiki-registry、glob 路径维护或用户提到飞书接口文档时使用。
---

# game-api-sync

飞书 Wiki 为唯一权威源。文档快照由 ECS 提供；成员**不安装 lark-cli**。

## 使用时机

- 用户要对齐某模块代码到飞书文档
- 用户要对比文档与当前实现（只读报告）
- 飞书刚改完文档，需要刷新 ECS 快照后再对齐
- 用户要把代码变更写成飞书文档草稿（标题带「agent生成，待审查」）
- 需要列出 ECS 已有快照模块或拉取某模块 snapshot

## 前置条件

1. **Agent 先在终端执行环境变量**（见 `references/env-setup.md`，每次调用 ECS 前必做，勿让用户手动）
2. 当前 Git 分支是开发者**有意工作的分支**（不切换分支、不开 PR）

## 指令

**每一步调用 ECS 前**，若尚未在本终端执行过环境变量，先运行 `references/env-setup.md` 中的三行 PowerShell。

### 对齐代码到文档

1. 确认模块名；必要时 `GET /api/snapshot/modules`
2. `GET /api/snapshot?module=<模块名>` 取快照
3. **划定文件范围**（必读 `references/registry-globs.md`）：
   - 从 `config/wiki-registry.yaml` 读本仓 `client_glob` / `server_glob` 并解析命中文件；
   - **结合用户要求**（@ 文件、指定目录、排除项）；
   - glob 不准或命中异常时 **自行查看仓库目录/搜索**，合并去重后得到最终文件集。
4. 用户或 Agent 发现**不在 glob 中**的协议文件时，**更新** `wiki-registry.yaml` 中对应 glob（优先改为显式路径列表），再继续对齐。
5. 只改最终范围内的**已有**协议源文件；**禁止** `Generated/`
6. 列出变更摘要与 registry 是否有改动；由用户自行 commit

### 对比文档与实现（只读）

1. 按 `references/registry-globs.md` 合并 glob、用户指定与必要目录排查，得到最终文件集
2. 若发现漏网协议文件，先更新 `wiki-registry.yaml` 中 glob（可选，但推荐与对齐流程一致）
3. `POST /jobs/api-compare`（Body：`module`、`repo`、`files` 路径→全文）
4. 展示 `report_md` 与 `defects`（按章节/方向/消息 + 字段类型）；**不改代码**

### 刷新 ECS 缓存

`POST /jobs/refresh-cache`，Body：`{"module":"<模块名>"}` 或 `{}` 全量

### 同步文档草稿到飞书

用户说「根据当前代码变更，生成飞书文档更新草稿」时：

1. 先执行 `references/env-setup.md` 中的环境变量。
2. **必读** `references/doc-write-format.md`：模式 A → **h2 主题**、**禁止 h1 子主题**、**禁止 caption 当分区**、**禁止 docx_draft 含【合并位置】**；enum+type → **两个 pre**；**禁止**实例行。ECS 按 `repo` 插入 **h1 客户端/服务端** 分区末尾。
3. 生成 **DocxXML**（伪 TS），`target` 与 `repo` 一致。
4. `POST /jobs/api-doc-sync`（`docx_draft` 与 `summary` 至少其一）。
5. 回复贴出 **docx_draft** 全文（不含【合并位置】）；说明审阅后去掉「（agent生成，待审查）」即可。

### 禁止

- 本机 `lark-cli`、自动 PR、切换分支、写入 `Generated/`

## 参考资料

- `references/doc-write-format.md` — **代码→飞书** 写文档格式（必读）
- `references/env-setup.md` — Agent 自动执行的环境变量
- `references/ecs-api.md` — ECS API 与请求示例
- `references/workflows.md` — 四种入口与 JSON Body
- `references/registry-globs.md` — **glob + 用户要求 + 目录排查 + 更新 registry**
