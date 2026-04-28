# server

基于 standalone Asio 协程的轻量级 TCP 房间服务端，当前已拆分为登录、大厅、商店、地图、战斗五个服务实例，共享同一份房间与用户状态。

## 功能概览

- 登录域：`REGISTER`、`LOGIN`、`LOGOUT`
- 大厅域：`EDIT_PROFILE`、`CREATE_ROOM`、`JOIN_ROOM`、`LEAVE_ROOM`、`LIST_ROOMS`、`SET_READY`
- 商店域：`SHOP_INIT`、`SHOP_MOVE_CURSOR`、`SHOP_BUY_ITEM`
- 地图域：`MAP_INIT`、`MAP_MOVE`
- 战斗域：`PLAYER_READY`、`PLAYER_MOVE`、`PLAYER_SHOOT`
- 响应信封分为短响应 `ShortEnvelope` 和长响应 `LongEnvelope`
- 运行时配置来自 `config/server.json`，商店目录来自 `config/shop_catalog.json`

## 当前架构

- 单进程、单 `asio::io_context` 事件循环
- 五个 server 实例共享一个 `ServerState`
- `Channel` 负责按行分帧、解析 JSON、调用 `dispatch_request`
- `Room` 统一承载房间准备、商店、地图和战斗状态
- `BattleServer` 内置固定 tick 循环，当前 tick 间隔为 16ms

默认端口配置：

- `authPort = 8765`
- `lobbyPort = 8766`
- `shopPort = 8767`
- `mapPort = 8768`
- `battlePort = 8769`

## 协议约定

### 1. 消息格式

- 传输层：TCP 文本帧，每条消息以换行符 `\n` 结尾
- 负载：JSON
- 最大消息长度：`Protocol::MAX_MESSAGE_SIZE = 65536`
- 所有 `type` 字段都使用整型枚举值，不使用字符串命令名

### 2. 命令类型

登录域 `Protocol::LoginRequestType`：

- `LOGIN = 0`
- `REGISTER = 1`
- `LOGOUT = 2`
- `ERROR = 100`

大厅域 `Protocol::HomeRequestType`：

- `CREATE_ROOM = 0`
- `JOIN_ROOM = 1`
- `LEAVE_ROOM = 2`
- `LIST_ROOMS = 3`
- `SEND_MESSAGE = 4`（保留）
- `HEARTBEAT = 5`（历史保留值）
- `EDIT_PROFILE = 6`
- `SET_READY = 7`
- `BROADCAST = 8`（服务端房间内推送使用）
- `GET_STATE_STATUS = 9`（预留）
- `ERROR = 100`

商店域 `Protocol::ShopResponseType`：

- `SHOP_INIT = 0`
- `SHOP_MOVE_CURSOR = 1`
- `SHOP_BUY = 2`
- `ERROR = 100`

地图域：

- `MapRequestType::MAP_INIT = 0`
- `MapRequestType::MAP_MOVE = 1`
- `MapResponseType::MAP_INIT = 0`
- `MapResponseType::MAP_SYNC = 1`

战斗域：

- `BattleRequestType::PLAYER_READY = 0`
- `BattleRequestType::PLAYER_MOVE = 1`
- `BattleRequestType::PLAYER_SHOOT = 2`
- `BattleResponseType::BATTLE_WAIT = 0`
- `BattleResponseType::BATTLE_FRAME = 1`
- `BattlePushMessageType::BATTLE_START = 0`

### 3. Envelope 语义

短响应 `ShortEnvelope`：

```json
{
  "code": 1,
  "message": "ok",
  "data": {}
}
```

- `code`：位掩码错误码，定义见 `Protocol::Code`
- `message`：由 `ShortEnvelope::map_message_from_code` 映射生成
- `data`：成功时放业务响应体，失败时通常为空对象

长响应 `LongEnvelope`：

```json
{
  "type": 8,
  "data": {},
  "pushMessages": []
}
```

- `type`：当前长响应所属命令或推送类型
- `data`：业务数据，不包含 `code`/`message`
- `pushMessages`：附加推送语义列表
- `NoResponseRsp`：表示该请求成功后不发送 direct response，由服务端改走异步广播

### 4. 当前实时推送行为

`SET_READY` 成功后会向房间成员广播：

- 推送信封：`LongEnvelope`
- `type = 8`（`HomeRequestType::BROADCAST`）
- `data = {"uid": "变更用户", "ready": true/false, "roomInfo": {...}}`
- 当房间内所有成员都 ready 时，`pushMessages = [0]`
  - 该 `0` 是大厅侧的“全员已准备好”附加语义，由客户端自行解释

`LEAVE_ROOM` 成功后会向剩余成员广播：

- 推送信封：`LongEnvelope`
- `type = 8`（`HomeRequestType::BROADCAST`）
- `pushMessages = []`
- `data = {"uid": "离房用户", "roomInfo": {...}}`

战斗准备阶段：

- `PLAYER_READY` 未全员就绪时广播 `BATTLE_WAIT`
- 全员就绪时立即触发一次首帧广播：
  - `type = 1`（`BattleResponseType::BATTLE_FRAME`）
  - `pushMessages = [0]`（`BattlePushMessageType::BATTLE_START`）

战斗帧同步：

- `BattleServer` 每 16ms 扫描房间并广播 `BATTLE_FRAME`
- `PLAYER_MOVE` 只记录输入方向
- 玩家坐标在 `tick_battle()` 中按帧更新，速度来自 `BattlePlayerAttribute::velocity`
- 当前实现不会在每帧移动应用后自动清零方向；玩家会沿最近一次记录的方向持续移动，直到收到新的移动输入或被其他逻辑修改  

### 5. 关键数据结构

`RoomInfo`：

- `roomId`
- `maximumPeople`
- `basicInfos`
- `readyUids`

`BattlePlayerEntity`：

- 继承 `BattleEntity`
- 包含 `uid`
- 包含 `attribute.velocity`，当前默认值为 `0.25`
- 包含已购买道具 `items`

## 项目结构

- `include/types.h`：基础协议枚举、错误码、`RoomInfo`、地图节点等共享类型
- `include/protocol.h`：登录/大厅/商店/地图协议与 Envelope 定义
- `include/battle.h`：战斗实体、战斗请求/响应、战斗事件 DTO
- `include/server.h`：`Server` 基类和五个服务子类
- `include/room.h`：房间聚合状态，包含商店、地图、战斗子状态
- `include/channel.h`：连接读写与消息处理接口
- `src/server.cpp`：登录/大厅/商店/地图服务逻辑和广播
- `src/battle_server.cpp`：战斗服务分发与 tick 广播逻辑
- `src/room.cpp`：房间状态迁移、地图和战斗帧计算
- `src/channel.cpp`：按行分帧、JSON 解析、请求分发
- `src/main.cpp`：配置加载与五个服务启动
- `config/server.json`：端口配置
- `config/shop_catalog.json`：商店物品目录
- `tests/unit_tests/`：单元测试目录（`server_channel_behavior_tests.cpp`、`collision_detection_tests.cpp`）
- `tests/stress_tests/`：压测脚本与场景

## 构建

依赖要求：

- CMake 3.21+
- C++20 编译器
- Ninja（推荐）
- `asio`、`nlohmann_json`、`GTest`（缺失时可自动下载）

默认 `SERVER_FETCH_DEPS=ON`，缺失依赖时自动通过 `FetchContent` 拉取。

当前 CMake 还启用了预编译头：

- `server` 目标预编译 `asio.hpp`、`nlohmann/json.hpp` 和常用 STL 头
- `unit_tests` 通过 `target_precompile_headers(... REUSE_FROM server)` 复用同一份 PCH

常用构建命令：

```bash
cmake --preset debug
cmake --build --preset debug
```

```bash
cmake --preset release
cmake --build --preset release
```

## 单元测试

```bash
cmake --preset release
cmake --build --preset release --target unit_tests
ctest --test-dir build --output-on-failure

# 运行不同测试文件
ctest --test-dir build -R "^protocol_tests::" --output-on-failure
ctest --test-dir build -R "^room_behavior_tests::" --output-on-failure
ctest --test-dir build -R "^server_auth_room_lifecycle_tests::" --output-on-failure
ctest --test-dir build -R "^server_dispatch_tests::" --output-on-failure
ctest --test-dir build -R "^server_error_paths_tests::" --output-on-failure
ctest --test-dir build -R "^collision_detection_tests::" --output-on-failure
```

单元测试当前覆盖：

- 房间成员增删与 ready 状态
- 房间信息构造
- 地图初始化与同步提交流程
- 战斗 ready、首帧生成、子弹生成
- 关键枚举值稳定性
- 主要服务错误路径

## 运行

默认从 `config/server.json` 读取配置：

```bash
./build/server
```

也可以显式指定配置文件：

```bash
./build/server --config config/server.json
```

启动后会同时监听五个端口：

- `auth-port`：登录/注册/登出
- `lobby-port`：资料与房间生命周期
- `shop-port`：商店阶段同步
- `map-port`：地图阶段同步
- `battle-port`：战斗阶段同步

## 碰撞检测 Demo

新增了一个独立的控制台 demo：`collision_demo`，演示轴对齐长方形（AABB）之间的 2D 碰撞检测。

- 物体均为长方形
- 边界与坐标轴平行
- 使用 AABB vs AABB 重叠判定

构建并运行：

```bash
cmake --preset debug
cmake --build --preset debug --target collision_demo collision_detection_tests
./build/debug/collision_demo
```

## 示例请求

注册：

```json
{"type":1}
```

登录：

```json
{"type":0,"uid":"1001"}
```

创建房间：

```json
{"type":0,"uid":"1001","maximumPeople":4}
```

战斗移动：

```json
{"type":1,"uid":"1001","input":{"x":1.0,"y":0.0}}
```

使用 `nc` 快速调试时，每条 JSON 后必须换行：

```bash
nc 127.0.0.1 8765
nc 127.0.0.1 8766
nc 127.0.0.1 8767
nc 127.0.0.1 8768
nc 127.0.0.1 8769
```