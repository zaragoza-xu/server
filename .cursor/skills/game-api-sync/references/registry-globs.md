# wiki-registry 路径（glob）与对齐范围

`config/wiki-registry.yaml` 的 `module_map.<模块>.client_glob` / `server_glob` 是**默认协议文件范围**，不是唯一真理。对齐、对比时必须结合 **glob、用户要求、仓库实况** 三者。

## 确定要读/要改哪些文件

按顺序执行（可合并去重）：

1. **读 registry**  
   读本仓对应键：`client` → `client_glob`，`server` → `server_glob`。支持字符串、YAML 列表、单文件路径（见 `registry_globs.py`）。

2. **服从用户明确要求**（优先级高于 glob）  
   - 用户 @ 了具体文件、文件夹，或说「只看 / 只改某某路径」→ 必须纳入范围。  
   - 用户说「不要动某某文件」→ 从范围中排除，即使 glob 命中。

3. **必要时自行查看目录**  
   在以下情况用终端或 IDE 列目录、搜索（消息名、协议号、模块关键词）：  
   - glob 命中 **0 个** 或 **明显过多**（误匹配 UI、测试等）；  
   - 快照里的消息名在 glob 命中的文件中找不到；  
   - 用户要求对齐但未给路径。  
   只把**协议相关**源文件（消息 struct、enum、序列化/协议头）加入候选，并在回复中**列出路径清单**供确认（`_status: draft` 时必须先列清单再改代码）。

4. **合并最终文件集**  
   `最终范围 = glob 命中 ∪ 用户指定 ∪ 合理补充发现`，去重。  
   - **对比**：将最终范围内文件全文放入 `POST /jobs/api-compare` 的 `files`。  
   - **对齐**：只修改最终范围内的**已有**文件；禁止 `Generated/`。

## 更新 glob（用户或 Agent 发现漏网文件时）

当用户通过 **@文件**、**/agent**、或对话指明「这些也是本模块协议文件」，且它们**不在当前 glob 结果中**时，Agent **应自行更新**本仓库 `config/wiki-registry.yaml`（同一 PR / 同一会话内完成，勿只改代码不更新 registry）：

1. 打开 `module_map.<模块名>` 下本仓字段（`client_glob` 或 `server_glob`）。  
2. **优先改为显式路径列表**（YAML 数组），把既有正确路径与新增路径都写上，避免继续用过宽的 `*Battle*` 一类模式。  
3. 若仍用通配符，收窄到真实目录（例如 `Assets/Scripts/Protocol/Battle/**/*.cs`），并在 `_notes` 注释中说明范围（可选）。  
4. 向用户简短说明 registry 变更（新增/删除了哪些路径模式）。  
5. 若 ECS 也部署了中央 `wiki-registry.yaml`，提醒管理员同步 server/client 两份及 ECS 上 `module_map`（飞书 `modules` token 无需因路径而改）。

**勿**把与模块无关的文件写入 glob（UI、Editor、测试、工具脚本等）。

## 与 `_status` 的配合（若 YAML 中有标注）

| `_status` | 行为 |
|-----------|------|
| `draft` | 必须列最终文件清单并得到用户认可后再改代码；glob 更新后仍建议列清单 |
| `candidate` | 改代码前列出将修改的文件 |
| `verified` | 可按合并后的范围执行；若发现 glob 漏文件仍须更新 registry |

## 示例

用户：`/agent 对齐战斗模块，包含 @Assets/Scripts/Net/BattleGate.cs`  
registry 中 `client_glob` 仅有 `**/*Battle*.cs` 且未命中 `BattleGate.cs`：

1. 将 `BattleGate.cs` 纳入对齐与对比范围；  
2. 把 `client_glob` 更新为包含该路径的列表或收窄后的 glob；  
3. 再按快照改协议字段。
