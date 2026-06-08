# AGENTS.md
## 项目结构速览

- `include/protocol.h`：登录/大厅/商店/地图等通用协议 DTO、枚举、Envelope。
- `include/battle.h`：战斗协议 DTO、战斗实体、事件、请求/响应枚举。
- `include/server.h`：Server 派生服务、共享状态、房间/用户状态入口。
- `include/room.h`：房间状态机、商店/地图/战斗房间行为接口。
- `src/server.cpp`：登录/大厅/商店服务处理、命令表分发。
- `src/map.cpp`：地图服务处理。
- `src/battle_server.cpp`：战斗服务处理和帧推送循环。
- `src/battle_room.cpp`：战斗房间内实体、同步、射击、刷怪、结算逻辑。
- `src/channel.cpp`：TCP JSON 行协议收发。
- `config/wiki-registry.yaml`：飞书模块到代码文件范围的映射。

## 常用检查命令

- 构建：`cmake --build build`
- 测试：`ctest --test-dir build`
- 调试测试：`ctest --test-dir build/debug-tests`

## 工程约定

- 函数命名短而准确，不要 Java 式冗长。
- 优先沿用现有命令表、Envelope、DTO、Room 状态机风格。
- 阶段流问题优先收敛到统一状态/phase gate，不要散落布尔判断。
- 不做无关重构，不回滚用户已有改动。

## 飞书接口文档协作

飞书 Wiki 是协议唯一权威源。文档快照与写回草稿通过 ECS 完成，本机不运行 `lark-cli`。

详细流程遵循 `.github/copilot-instructions.md` 和 `.github/game-api-sync/` 下文档。

### 关键约束

- 调用 ECS 前，按 `.github/game-api-sync/env-setup.md` 设置环境变量。
- 对比文档与实现时只读，不改代码。
- 对齐协议时只改范围内已有文件，禁止新建 `Generated/` 或平行协议目录。
- 漏网协议文件需要更新 `config/wiki-registry.yaml`。
- 禁止自动开 PR、切换分支、本机运行 `lark-cli`。