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
2. **`POST /jobs/refresh-cache`**，Body：`{"module":"<模块名>"}`（**仅当前模块**；默认 **revision 未变则跳过**全量拉取，见下）
3. `GET /api/snapshot?module=<模块名>` 取快照
4. **划定文件范围**（必读 `references/registry-globs.md`）：
   - 从 `config/wiki-registry.yaml` 读本仓 `client_glob` / `server_glob` 并解析命中文件；
   - **结合用户要求**（@ 文件、指定目录、排除项）；
   - glob 不准或命中异常时 **自行查看仓库目录/搜索**，合并去重后得到**待对齐路径清单**。
5. **Glob 门禁（改代码前必做）**：
   - 对清单中每个待改路径，检查是否已被当前模块 glob 命中；
   - 可运行（在游戏仓根目录）：  
     `python <中央仓>/scripts/check_glob_for_align.py --module <模块> --repo client|server --paths <路径...> [--user-explicit <用户@的路径...>]`
   - **不在 glob 中**且为协议源文件（含消息 struct / enum）→ **自动更新** `config/wiki-registry.yaml`：在 `module_map.<模块>` 的 `client_glob` / `server_glob` 中**追加显式路径**（优先 YAML 列表，保留原有正确条目）；
   - **勿自动加入 glob**：`.prefab` / `.unity` / `.asset` / 图片音频、`Resources/`、`Editor/`、`Tests/`、纯 UI/ViewModel/State 视图层（用户 **@ 明确指定** 时除外）；
   - 更新 registry 后 **暂停并提醒用户核对 glob 路径**（列出新增项与 `skipped_auto_add` 原因），用户确认或修正后再继续改代码。
6. 只改最终范围内的**已有**协议源文件；**禁止** `Generated/`
7. 列出变更摘要与 registry 是否有改动；由用户自行 commit

### 对比文档与实现（只读）

1. **`POST /jobs/refresh-cache`**，Body：`{"module":"<模块名>"}`（**仅当前模块**；默认 revision 比对，未变则 `skipped`）
2. 按 `references/registry-globs.md` 合并 glob、用户指定与必要目录排查，得到最终文件集
3. 若发现漏网协议文件，先更新 `wiki-registry.yaml` 中 glob（可选，但推荐与对齐流程一致）
4. `POST /jobs/api-compare`（Body：`module`、`repo`、`files`；**须含** `config/message_aliases.yaml` 全文；可选 `target`、`scoped: true`）
5. 展示 `report_md` 与 `defects`（支持 `config/message_aliases.yaml` 别名匹配）；**不改代码**

### 刷新 ECS 缓存

`POST /jobs/refresh-cache`：

| Body | 行为 |
|------|------|
| `{"module":"<模块名>"}` | 单模块；**先比对飞书 `revision_id`**，与缓存一致则跳过全量拉取 |
| `{"module":"<模块名>","force":true}` | 跳过 revision 比对，**强制**全量拉取 |
| `{}` | 全量模块（同样默认 revision 比对） |
| `{"force":true}` | 全量强制刷新 |

用户说「飞书刚改完 / 强制刷新」时用 `"force": true`。响应含 `modules`（已刷新）、`skipped`（revision 未变跳过）。

### 同步文档草稿到飞书

用户说「根据当前代码变更，生成飞书文档更新草稿」时：

1. 先执行 `references/env-setup.md` 中的环境变量。
2. 确定**模块名**与**变更路径**（`git diff` 或用户 @ 的文件）。
3. **在游戏仓根目录**运行（`<中央仓>` 为 game-api-sync 克隆路径）：

```powershell
python <中央仓>/scripts/agent_doc_draft.py `
  --module <模块名> --repo client|server `
  --paths <路径1> <路径2> ... `
  [--user-explicit <用户@的路径...>] `
  --apply-glob
```

   - 也可用 `--git-since origin/main` 代替 `--paths`（取相对 HEAD 的变更文件）。
   - `--apply-glob`：将漏网协议路径写入 `config/wiki-registry.yaml`；**必须提醒用户核对 git diff**。
   - 未加 `--apply-glob` 且 `needs_registry_update` 时，**先列出 `missing_from_glob` 并暂停**，待用户确认 registry 后再继续。
   - CLI 内部：`glob 检查` → `refresh-cache` → `compare` → 按 `sync_targets_for_module` 分流（**struct/class → api_docs，enum/interface → type_constraints**）→ `build_docx_draft`（标记 **agent生成，待审查**）。
   - 输出 JSON 的 `drafts[].api_doc_sync_body` 可直接用于下一步；`docx_draft` 为空或 `skipped` 时说明原因，勿手写 XML 除非 CLI 无法覆盖。

4. 对每个未 `skipped` 的 draft：`POST /jobs/api-doc-sync`（Body 用 `api_doc_sync_body`）；或加 `--sync` 由 CLI 一并 POST。
5. 回复：`user_action_required`（若有）、各 target 的 classification、**docx_draft** 摘要；说明审阅后去掉「（agent生成，待审查）」即可。

### 模块系统设计文档（PR merge · CI 写飞书）

PR 合并后 GitHub Actions 自动将**变更差量**写入飞书「模块系统设计」wiki。使用 **ModuleDocBot / 创建者** profile（`MODULE_DOC_LARK_CLI_HOME`），与协议 sync 的 GameBot **隔离**。

- CI：`run_system_doc_job.py` → `POST /jobs/module-system-doc-sync`
- 新模块无 `system_design_obj` 时建 wiki 子页；已有模块仅 append 变更段
- 首次创建后需人工写回 `system_design_obj` 到 `wiki-registry.yaml`

**禁止**默认跳过 CLI 手写 DocxXML。格式细节见 `references/doc-write-format.md`（兜底）。

### 禁止

- 本机 `lark-cli`、自动 PR、切换分支、写入 `Generated/`

## 参考资料

- `references/doc-write-format.md` — **代码→飞书** 写文档格式（必读）
- `references/env-setup.md` — Agent 自动执行的环境变量
- `references/ecs-api.md` — ECS API 与请求示例
- `references/workflows.md` — 四种入口与 JSON Body
- `references/registry-globs.md` — **glob + 用户要求 + 目录排查 + 更新 registry**
