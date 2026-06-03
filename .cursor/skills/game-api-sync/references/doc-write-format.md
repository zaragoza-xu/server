# 代码 → 飞书文档格式（Agent 必读）

完整版：`docs/feishu-doc-write-format.md` §3.1、§4.6。

## 写哪份文档

| 变更 | target |
|------|--------|
| 协议消息 | `api_docs` |
| 跨模块类型 | `type_constraints` |
| 单模块配置/表（仅类型） | `api_docs` |

## 模式 A（页内已有 h1 客户端 / h1 服务端）

1. **`h2 <主题名>（agent生成，待审查）`**（如「武器」），禁止 **`h1` 子主题**。
2. **`docx_draft` 禁止** `<p>【合并位置】…</p>`（ECS 会删掉；也不要写进飞书）。插入位置由 ECS 按 `repo` 放到对应 **h1 分区末尾**。
3. 配置类 **不写 caption**；enum + type → **两个 pre**。
4. **禁止** 实例行（Knife 字面量、`.asset` 路径）。

## 模式 B

无 h1 客户端/服务端 时，可用 **`h1 <主题名>（agent生成，待审查）`**。

## 协议消息（§4.3）

同节多方向时才用 `caption="客户端"` / `caption="服务端"`。

## 流程

snapshot → DocxXML（无合并位置段）→ `api-doc-sync` → 回复说明审阅要点即可。
