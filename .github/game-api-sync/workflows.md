# 四种入口（v2）

| 入口 | 触发 | ECS |
|------|------|-----|
| 刷新缓存 | IDE 主动 | `POST /jobs/refresh-cache` |
| 对比 | IDE 主动，只读 | `POST /jobs/api-compare` |
| 对齐代码 | IDE 主动 | `GET /api/snapshot` + 改现有文件 |
| 写回文档 | IDE 主动 | `POST /jobs/api-doc-sync` |

## api-compare / api-doc-sync

见中央仓 `.cursor/skills/game-api-sync/references/workflows.md`。

对比为 **消息级 + 字段类型级**（非全局字段名集合）；`repo` 须与要比对的文档方向一致（client/server）。
