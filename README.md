# server

基于 standalone Asio 协程的轻量级 TCP 房间服务端。当前拆分为登录、大厅、商店、地图、战斗五个服务实例，共享同一份房间与用户状态。

## 功能概览

- 登录域：`REGISTER`、`LOGIN`、`LOGOUT`
- 大厅域：`EDIT_PROFILE`、`CREATE_ROOM`、`JOIN_ROOM`、`LEAVE_ROOM`、`LIST_ROOMS`、`SET_READY`
- 商店域：`SHOP_INIT`、`SHOP_MOVE_CURSOR`、`SHOP_BUY`
- 地图域：`MAP_INIT`、`MAP_MOVE`
- 战斗域：`PLAYER_READY`、`BATTLE_SYNC`、`PLAYER_SHOOT`
- 响应分为 `ShortEnvelope` 和 `LongEnvelope`
- 运行时配置来自 `config/server.json`，商店目录来自 `config/shop_catalog.json`

## 当前架构

- 单进程、单 `asio::io_context` 事件循环
- 五个 `Server` 实例共享一个 `ServerState`
- `Channel` 负责按行分帧、解析 JSON、调用 `dispatch_request`
- `Room` 统一承载房间准备、商店、地图和战斗状态，并用内部阶段机约束入口顺序
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
- `BROADCAST = 8`（大厅内服务端广播使用）
- `GET_STATE_STATUS = 9`（预留）
- `ERROR = 100`

商店域：

- `ShopRequestType::SHOP_INIT = 0`
- `ShopRequestType::SHOP_MOVE_CURSOR = 1`
- `ShopRequestType::SHOP_BUY = 2`
- `ShopRequestType::ERROR = 100`
- `ShopResponseType::SHOP_SYNC = 0`

地图域：

- `MapRequestType::MAP_INIT = 0`
- `MapRequestType::MAP_MOVE = 1`
- `MapResponseType::MAP_INIT = 0`
- `MapResponseType::MAP_SYNC = 1`

战斗域：

- `BattleRequestType::PLAYER_READY = 0`
- `BattleRequestType::BATTLE_SYNC = 1`
- `BattleRequestType::PLAYER_SHOOT = 2`
- `BattleResponseType::BATTLE_WAIT = 0`
- `BattleResponseType::BATTLE_FRAME = 1`
- `BattlePushMessageType::BATTLE_START = 0`
- `BattlePushMessageType::BATTLE_END = 1`

### 3. Envelope 语义

短响应 `ShortEnvelope`：

```json
{
  "code": 1,
  "data": {},
  "message": "ok"
}
```

- `code` 是位掩码错误码，定义见 `Protocol::Code`
- `message` 由 `ShortEnvelope::map_message_from_code` 映射生成
- `data` 在成功时承载业务响应体，失败时通常为空对象

长响应 `LongEnvelope`：

```json
{
  "type": 0,
  "data": {},
  "pushMessages": []
}
```

- `type` 是当前长响应所属命令或推送类型
- `data` 承载业务数据，不包含 `code`/`message`
- `pushMessages` 是附加推送语义列表
- `NoResponseRsp` 表示该请求成功后不发送 direct response，而是仅走异步广播

### 4. 当前实时推送行为

`SET_READY` 成功后会向房间成员广播：

- 推送信封：`LongEnvelope`
- `type = 8`（`HomeRequestType::BROADCAST`）
- `data = {"uid": "变更用户", "ready": true/false, "roomInfo": {...}}`
- 当房间内所有成员都 ready 时，`pushMessages = [0]`
  - `0` 对应 `HomePushMessageType::ALL_READY`
  - 房间阶段从 `LOBBY` 推进到 `SHOP`

`LEAVE_ROOM` 成功后会向剩余成员广播：

- 推送信封：`LongEnvelope`
- `type = 8`（`HomeRequestType::BROADCAST`）
- `pushMessages = []`
- `data = {"uid": "离房用户", "roomInfo": {...}}`

商店阶段：

- `SHOP_INIT` 返回 direct `LongEnvelope`
- `SHOP_MOVE_CURSOR` 与 `SHOP_BUY` 成功后不直返，由服务端广播 `SHOP_SYNC`
- 广播 `data` 当前仅包含 `items`

战斗准备阶段：

- `PLAYER_READY` 只有在 `MAP` 阶段且已经提交当前地图节点后才会成功
- `PLAYER_READY` 未全员就绪时广播 `BATTLE_WAIT`
- 全员就绪时立即触发一次首帧广播：
  - `type = 1`（`BattleResponseType::BATTLE_FRAME`）
  - `pushMessages = [0]`（`BattlePushMessageType::BATTLE_START`）
  - 房间阶段从 `MAP` 推进到 `BATTLE`

战斗帧同步：

- `BattleServer` 每 16ms 扫描房间并广播 `BATTLE_FRAME`
- `BATTLE_SYNC` 接收客户端实际玩家位置和本地计算的怪物位置
- 玩家位置以客户端最新上报为准；怪物位置在 `tick_battle()` 中按实体取各客户端最新上报的均值
- `PLAYER_SHOOT` 由服务端生成玩家子弹；tick 中推进子弹，命中墙/敌人时广播 hit、damage、destroy 事件
- 战斗结束的帧会在 `pushMessages` 中附带 `BATTLE_END`

### 5. 当前阶段行为补充

房间阶段状态机：

- 初始阶段为 `LOBBY`
- `LOBBY`：允许建房、加入、离房、房间 ready；全员 ready 后进入 `SHOP`
- `SHOP`：允许 `SHOP_INIT`、`SHOP_MOVE_CURSOR`、`SHOP_BUY`；首次合法 `MAP_INIT` 后进入 `MAP`
- `MAP`：允许 `MAP_INIT`、`MAP_MOVE`；全员提交同一个合法节点后，才允许战斗 ready
- `BATTLE`：允许 `PLAYER_READY` 启动后的 `BATTLE_SYNC`、`PLAYER_SHOOT` 和服务端 tick；商店/地图/大厅 ready 等非战斗入口会返回房间状态错误
- `END`：本轮流程结束；失败或最后节点胜利会进入该阶段
- 胜利且当前地图节点仍有后继时，战斗结束后回到 `MAP`；失败或最后节点胜利时进入 `END`
- 成员离房/登出清理不受阶段限制；新成员只能在 `LOBBY` 阶段加入

商店阶段：

- 商店目录在服务启动时从 `config/shop_catalog.json` 读入 `ServerState`
- `Room` 构造时会快照目录版本和完整物品列表，并随机打乱一次顺序
- 房间当前可见商店物品数量等于 `min(当前房间人数, 物品库大小)`
- `SHOP_MOVE_CURSOR` 的 `itemId` 不合法时，会被当成取消选择，而不是失败
- 商店接口只在 `SHOP` 阶段成功；其他阶段返回 `ROOM_STATE_ERROR`

地图阶段：

- `MAP_INIT` 首次请求时按房间懒生成共享地图
- `MAP_INIT` 只在 `SHOP` 或 `MAP` 阶段成功；第一次成功调用会把房间从 `SHOP` 推进到 `MAP`
- 第一列和最后一列都是单节点；最后一列节点类型为 `BOSS`
- 相邻两列连接满足：无交叉、允许一对多、允许多对一、并覆盖后一列全部节点
- `MAP_MOVE` 只有当房间内所有成员选择了同一个可提交节点时，才会提交并推进到该节点
- 首次提交必须从根节点开始；后续提交必须是当前节点的合法后继
- `MAP_MOVE` 只在 `MAP` 阶段成功

战斗阶段：

- `PLAYER_READY` 只在 `MAP` 阶段成功，且要求当前已提交地图节点
- 全员 battle ready 后，服务端创建玩家实体、按当前地图节点类型生成敌人，并进入 `BATTLE`
- `BATTLE_SYNC`、`PLAYER_SHOOT`、`tick_battle()` 只在 `BATTLE` 阶段成功
- 战斗胜利会清空战斗临时状态；若当前节点还有后继则回到 `MAP`，否则进入 `END`
- 玩家全灭按失败处理，清空战斗临时状态并进入 `END`

## 项目结构

- `include/types.h`：基础协议枚举、错误码、`RoomInfo`、`MapNode` 等共享类型
- `include/protocol.h`：登录/大厅/商店/地图协议与 Envelope 定义
- `include/battle.h`：战斗实体、战斗请求/响应、战斗事件 DTO
- `include/server.h`：`Server` 基类和五个服务子类
- `include/room.h`：房间聚合状态，包含商店、地图、战斗子状态
- `include/channel.h`：连接读写与消息处理接口
- `src/server.cpp`：登录/大厅/商店/地图服务逻辑和广播
- `src/battle_server.cpp`：战斗服务分发与 tick 广播逻辑
- `src/room.cpp`：房间阶段迁移、商店和地图状态逻辑
- `src/battle_room.cpp`：战斗启动、同步、tick 推进和结算
- `src/map.cpp`：地图列生成和无交叉路径连接逻辑
- `src/channel.cpp`：按行分帧、JSON 解析、请求分发
- `src/main.cpp`：配置加载与五个服务启动
- `config/server.json`：端口配置
- `config/shop_catalog.json`：商店物品目录
- `tests/unit_tests/`：单元测试目录（`*_tests.cpp`，主要覆盖房间/协议路由/错误码等纯逻辑）
- `tests/network_tests/`：网络与集成测试目录（TCP 分帧、端到端链路、多服务协同）
- `tests/stress_tests/`：压测脚本与场景

## 构建

依赖要求：

- CMake 3.21+
- C++20 编译器
- Ninja（推荐）
- `asio`、`nlohmann_json`、`GTest`（缺失时可自动下载）

默认 `SERVER_FETCH_DEPS=ON`，缺失依赖时自动通过 `FetchContent` 拉取。

当前 CMake 构建布局：

- 默认构建为 `Release`
- 默认 `SERVER_BUILD_TESTS=OFF`
- 业务源码先编成 `server_core` 静态库，再由 `server` 与各测试目标复用
- `server_core` 使用预编译头缓存 `asio.hpp`、`nlohmann/json.hpp` 和常用 STL 头
- 测试目标通过 `target_precompile_headers(... REUSE_FROM server_core)` 复用同一份 PCH

常用构建命令：

```bash
cmake --preset release
cmake --build --preset release
```

```bash
cmake --preset debug
cmake --build --preset debug
```

## 单元测试

项目当前包含一组专用测试预设：

```bash
cmake --preset debug-tests
cmake --build --preset debug-tests
ctest --preset debug-tests
```

也可以按测试名过滤：

```bash
ctest --test-dir build/debug-tests -R "^map_tests::" --output-on-failure
ctest --test-dir build/debug-tests -R "^protocol_tests::" --output-on-failure
ctest --test-dir build/debug-tests -R "^room_behavior_tests::" --output-on-failure
ctest --test-dir build/debug-tests -R "^server_auth_room_lifecycle_tests::" --output-on-failure
ctest --test-dir build/debug-tests -R "^server_dispatch_tests::" --output-on-failure
ctest --test-dir build/debug-tests -R "^server_error_paths_tests::" --output-on-failure
ctest --test-dir build/debug-tests -R "^collision_detection_tests::" --output-on-failure
```

单元测试当前覆盖：

- 协议枚举值稳定性和 Envelope JSON 往返
- 房间成员增删与 ready 状态
- 房间阶段状态机：未选地图节点不能 battle ready、战斗中拒绝非战斗接口
- 商店物品数量随当前房间人数变化
- 地图生成满足无交叉、全覆盖与层级一致性
- 地图初始化与同步提交流程
- 战斗 ready、首帧生成、子弹生成、胜利回地图/失败进结束态
- 主要服务错误路径与分发路由

## 网络/集成测试

网络测试使用真实 TCP 端到端连接，主要覆盖：

- `tests/network_tests/fake_client.h`：测试客户端，按换行符 `\n` 分帧发送/接收 JSON
- `tests/network_tests/channel_network_tests.cpp`：`Channel` 分帧行为（CRLF、多帧合并、空行跳过、超长载荷处理/连接关闭）
- `tests/network_tests/login_network_tests.cpp`：登录域（`REGISTER`/`LOGIN`/错误 JSON）
- 多服务端到端（共享同一份 `ServerState`）：
  - `tests/network_tests/home_network_tests.cpp`：`CREATE_ROOM` / `LIST_ROOMS` / `JOIN_ROOM`
  - `tests/network_tests/shop_network_tests.cpp`：`SHOP_INIT` / `SHOP_MOVE_CURSOR`（异步推送）
  - `tests/network_tests/map_network_tests.cpp`：`MAP_INIT` / `MAP_MOVE`（异步推送）
  - `tests/network_tests/battle_network_tests.cpp`：`LOBBY -> SHOP -> MAP -> PLAYER_READY` 后进入 `BATTLE_WAIT`/`BATTLE_FRAME`

构建：

```bash
cmake --preset release
cmake --build --preset release --target unit_tests
```

运行：

```bash
ctest --test-dir build -R "network_tests::" --output-on-failure
```

也可按文件名单独跑（例如 Home）：

```bash
ctest --test-dir build -R "^home_network_tests::" --output-on-failure
```

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

- `authPort`：登录/注册/登出
- `lobbyPort`：资料与房间生命周期
- `shopPort`：商店阶段同步
- `mapPort`：地图阶段同步
- `battlePort`：战斗阶段同步

## 碰撞检测 Demo

项目包含独立的控制台 demo：`collision_demo`，用于演示轴对齐长方形（AABB）之间的 2D 碰撞检测。

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

战斗同步：

```json
{"type":1,"uid":"1001","playerPosition":{"x":1.0,"y":0.0},"playerDirection":{"x":1.0,"y":0.0},"enemyPositions":[{"entityId":2,"position":{"x":4.0,"y":5.0},"direction":{"x":0.0,"y":-1.0}}]}
```

使用 `nc` 快速调试时，每条 JSON 后必须换行：

```bash
nc 127.0.0.1 8765
nc 127.0.0.1 8766
nc 127.0.0.1 8767
nc 127.0.0.1 8768
nc 127.0.0.1 8769
```
