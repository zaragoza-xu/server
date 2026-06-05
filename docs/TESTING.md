# 测试集成指南

本文档描述 **game-test-harness** / **Automation 包** 与本服务端联调时的启动方式、端口契约与可选观测接口。

## 启动服务端

### 推荐：测试脚本（默认 5 秒战斗）

```bash
chmod +x scripts/run_test_server.sh
./scripts/run_test_server.sh
```

脚本行为：

- `cd` 到仓库根目录
- 默认 `./build/server --config config/server.json --duration-seconds 5`
- 透传额外 CLI 参数；若已指定 `--duration-seconds` 则不再追加默认值

### WSL / 手动启动

```bash
cmake --preset release && cmake --build --preset release
./build/server --config config/server.json --duration-seconds 5
```

### CLI 参数

| 参数 | 说明 | 默认 |
|------|------|------|
| `--config <path>` | 五端口配置 | `config/server.json` |
| `--battle-config <path>` | 指定 battle JSON 路径 | 搜索 `config/battle_config.json` |
| `--duration-seconds <n>` | 覆盖战斗时长（秒，须 > 0） | JSON 内值（通常 180） |

启动日志会打印：`Battle config: durationSeconds=...`

## 五端口契约

| 服务 | 默认端口 | 用途 |
|------|---------|------|
| Auth | 8765 | REGISTER / LOGIN / LOGOUT |
| Lobby | 8766 | 房间、SET_READY、GET_STATE_STATUS |
| Shop | 8767 | SHOP_INIT / MOVE / BUY |
| Map | 8768 | MAP_INIT / MAP_MOVE |
| Battle | 8769 | PLAYER_READY / POSITION_SYNC / PLAYER_SHOOT |

传输：TCP，一行 JSON + `\n`。

Automation `NetworkEndpointConfig` 示例：

```json
{
  "host": "127.0.0.1",
  "loginPort": 8765,
  "homePort": 8766,
  "shopPort": 8767,
  "mapPort": 8768,
  "battlePort": 8769
}
```

## 阶段推进（Automation 须遵守）

1. REGISTER → LOGIN（每实例一 uid）
2. CREATE_ROOM / JOIN_ROOM → 全员 SET_READY → `BROADCAST` + `pushMessages=[0]`（ALL_READY）→ **SHOP**
3. 可选商店操作 → MAP_INIT → 全员 MAP_MOVE 同一 selectId
4. 全员 PLAYER_READY → BATTLE → 周期 POSITION_SYNC（type=**1**）→ BATTLE_END（pushMessages 含 1）

**离房顺序**：先 `LEAVE_ROOM`，再 `LOGOUT`（登出不广播离房）。

## GET_STATE_STATUS（type=9，可选）

Lobby 端口，ShortEnvelope 响应。

请求：

```json
{"type": 9, "uid": "1001"}
```

成功时 `data` 字段：

| 字段 | 说明 |
|------|------|
| `online` | 是否在线 |
| `roomId` | 不在房为 -1 |
| `roomPhase` | 0=LOBBY, 1=SHOP, 2=MAP, 3=BATTLE, 4=END |
| `roomMemberCount` | 房间人数 |
| `allLobbyReady` | 大厅是否全员 ready |
| `mapNodeId` | 已提交地图节点，未提交 -1 |
| `battleTick` | 战斗 tick，非战斗为 0 |

uid 未登录 → `NOT_FOUND`。

Harness GUI 可轮询；CLI 场景仍可用 pushMessages，不强制依赖此接口。

## Harness 启动 server 子进程

```bash
/path/to/server/build/server --config /path/to/server/config/server.json --duration-seconds 5
```

Automation 战斗等待超建议：`durationSeconds + 10s` 缓冲。

## 验收清单（跨仓库）

- [ ] 单实例：register → login → create → ready → map → battle → leave → logout
- [ ] 双实例：同 room，SET_READY / MAP_MOVE / PLAYER_READY 同步
- [ ] `--duration-seconds 5` 下完整战斗 ≤15s 结束
- [ ] GET_STATE_STATUS：ready 后 roomPhase 0→1
