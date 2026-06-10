# 四种入口（v2）

| 入口 | 触发 | ECS |
|------|------|-----|
| 刷新缓存 | IDE 主动（对齐/对比/写回前先调，当前模块；**默认 revision 比对**） | `POST /jobs/refresh-cache` |
| 对比 | IDE 主动，只读 | `POST /jobs/api-compare` |
| 对齐代码 | IDE 主动 | refresh → `GET /api/snapshot` + 改现有文件 |
| 写回文档 | IDE 主动 / CI PR 合并 | `POST /jobs/api-doc-sync` |
| CI 自动 | PR **合并**时（任意目标分支） | `scripts/ci/run_sync_job.py`（仅 sync，无 compare 报告） |

## refresh-cache Body

```json
{ "module": "战斗", "force": false }
```

- 默认：ECS 用 `lark-cli docs +fetch --scope outline --max-depth 0` 探测飞书 `revision_id`，与本地快照一致则 **跳过** 全量拉取（响应 `skipped`）。
- `"force": true`：跳过比对，始终全量拉取并更新缓存。
- 用户说「飞书刚改完 / 强制刷新」→ Agent 传 `"force": true`。

## api-compare Body

```json
{
  "module": "战斗",
  "repo": "client",
  "files": {
    "Assets/Scripts/Battle/Foo.cs": "<文件全文>",
    "config/message_aliases.yaml": "<游戏仓 alias 文件全文>"
  },
  "target": null,
  "scoped": true
}
```

- `files` 除协议源文件外，**必须**包含本仓 `config/message_aliases.yaml`（ECS 无游戏仓目录，靠 body 传入别名表）

- `target`：`api_docs` / `type_constraints`；省略时按快照自动分流
- `scoped`：true 时仅报告 glob 命中文件内的代码类型（忽略跨模块共享头噪声）
- 别名：`config/message_aliases.yaml`（中文章节 → 英文类型名）

对比按 **章节 + 方向（client/server）+ 消息名** 分组，字段级比对 **name / type / optional**；`网络相关` 等模块额外提示 PacketType/MSG 常量。

## api-doc-sync Body

Agent **须先**运行 `scripts/agent_doc_draft.py`（见 SKILL「同步文档草稿到飞书」），再用输出的 `api_doc_sync_body`：

```json
{
  "module": "战斗",
  "repo": "client",
  "target": "api_docs",
  "summary": "变更说明",
  "files_changed": ["Assets/Scripts/Battle/Foo.cs"],
  "docx_draft": "<h2>武器（agent生成，待审查）</h2><pre lang=\"TypeScript\"><code>...</code></pre>"
}
```

`summary` 与 `docx_draft` 至少其一。分流规则与 CI 相同：**struct/class → api_docs**，**enum/interface → type_constraints**（`sync_targets_for_module`）。

**写文档格式**（CLI 兜底）：见 `doc-write-format.md`。glob 见 `registry-globs.md`。
