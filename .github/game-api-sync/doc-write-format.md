# 代码 → 飞书文档格式（Agent 必读）

完整版：`docs/feishu-doc-write-format.md` §3.1、§4.6。

## 模式 A（有 h1 客户端 / h1 服务端）

- **`h2 <主题>（agent生成，待审查）`**，禁止 h1 子主题
- **`docx_draft` 禁止** `<p>【合并位置】…</p>`
- 配置：**两个 pre**（enum / type），**不写 caption**
- **禁止** 实例行、资源路径

ECS 按 `repo` 插入对应 **h1 分区末尾**。

## 模式 B

无客户端/服务端 h1 时，可用 **h1 主题**。

## 协议消息（§4.3）

同节多方向时才用 `caption="客户端"` / `caption="服务端"`。
