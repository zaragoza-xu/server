# 四种入口（v2）

| 入口 | 触发 | ECS |
|------|------|-----|
| 刷新缓存 | IDE 主动 | `POST /jobs/refresh-cache` |
| 对比 | IDE 主动，只读 | `POST /jobs/api-compare` |
| 对齐代码 | IDE 主动 | `GET /api/snapshot` + 改现有文件 |
| 写回文档 | IDE 主动 | `POST /jobs/api-doc-sync` |

## api-compare Body

```json
{
  "module": "战斗",
  "repo": "client",
  "files": { "Assets/Scripts/Battle/Foo.cs": "<文件全文>" }
}
```

对比按 **章节 + 方向（client/server）+ 消息名** 分组，字段级比对 **name / type / optional**；`网络相关` 等模块额外提示 PacketType/MSG 常量。

## api-doc-sync Body

```json
{
  "module": "战斗",
  "repo": "client",
  "target": "api_docs",
  "summary": "变更说明",
  "files_changed": ["Assets/Scripts/Battle/Foo.cs"],
  "docx_draft": "<h2>武器（agent生成，待审查）</h2><pre lang=\"TypeScript\"><code>enum ...</code></pre><pre lang=\"TypeScript\"><code>type ...</code></pre>"
}
```

`summary` 与 `docx_draft` 至少其一。主题用 **`h2`（模式 A）** 或 **`h1`（模式 B）**，须带 **`（agent生成，待审查）`**；**禁止**【合并位置】段落；不用 callout。ECS 按 `repo` 插入 **h1 客户端/服务端** 分区末尾（无对应 h1 时 append 文末）。

**写文档格式**：见 `doc-write-format.md`。glob 见 `registry-globs.md`。
