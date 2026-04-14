# server

基于 standalone Asio 协程的轻量级 TCP 房间服务端。

## 功能概览

- 登录域命令：`LOGIN`、`REGISTER`、`LOGOUT`
- 大厅域命令：`CREATE_ROOM`、`JOIN_ROOM`、`LEAVE_ROOM`、`LIST_ROOMS`、`SET_READY`
- 响应信封分为短响应与长响应：`ShortEnvelope` / `LongEnvelope`
- 用户与房间状态由 `ServerState` 维护，并在两个服务实例间共享

## 当前协议（与代码一致）

### 1. 消息格式

- 传输层：TCP 文本帧，每条消息以换行符 `\n` 结尾
- 负载：JSON
- 最大消息长度：`Protocol::MAX_MESSAGE_SIZE = 65536`

### 2. 命令类型

`type` 字段是枚举数值，不是字符串。

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
- `SEND_MESSAGE = 4`（协议定义保留，服务端未打通）
- `EDIT_PROFILE = 6`（协议定义保留，服务端未打通）
- `SET_READY = 7`
- `ERROR = 100`

### 3. 返回结构

短响应（`ShortEnvelope`）：

```json
{
  "code": 1,
  "message": "ok",
  "data": {}
}
```

- `code`：位掩码错误码（见 `Protocol::Code`）
- `message`：由 `Envelope::map_message_from_code` 映射
- `data`：成功时放业务响应，空响应命令返回空对象

长响应（`LongEnvelope`）：

```json
{
  "type": 7,
  "data": {},
  "pushMessages": []
}
```

- `type`：请求类型（整型枚举值）
- `data`：业务响应体（不包含 `code/message`）
- `pushMessages`：推送消息类型列表（当前 direct long response 为空）

`SET_READY` 在房间内会触发实时推送：

- 推送信封：`LongEnvelope`
- `type = 7`（`SET_READY`）
- `pushMessages = []`
- `data = {"uid": "变更用户", "ready": true/false, "roomInfo": {...}}`

`LEAVE_ROOM` 成功后会向房间内剩余成员实时推送：

- 推送信封：`LongEnvelope`
- `type = 2`（`LEAVE_ROOM`）
- `pushMessages = []`
- `data = {"uid": "离房用户", "roomInfo": {...}}`

房间信息 `RoomInfo` 额外包含：

- `readyUids`：当前房间中处于准备状态的用户 uid 列表

### 4. 当前请求/响应类型

请求：

- `LoginReq`
- `LogoutReq`
- `CreateRoomReq`
- `JoinRoomReq`
- `LeaveRoomReq`
- `ListRoomsReq`
- `SetReadyReq`

响应：

- `RegisterRsp`
- `LoginRsp`
- `CreateRoomRsp`
- `JoinRoomRsp`
- `ListRoomsRsp`
- `SetReadyRsp`
- `EmptyRsp`（无业务数据返回的占位类型）

## 项目结构

- `include/protocol.h`：协议类型、错误码、JSON 序列化定义
- `include/server.h`：`Server`/`LoginServer`/`HomeServer` 接口与命令分发表
- `include/channel.h`：连接读写与消息处理接口
- `src/server.cpp`：连接接入、状态机和业务方法实现
- `src/channel.cpp`：按行分帧、JSON 解析、分发调用
- `src/main.cpp`：参数解析、实例启动
- `tests/unit_tests.cpp`：单元测试

`ServerState` 关键字段：

- `userData`：`uid -> PlayerData`
- `users`：`uid -> 在线会话`
- `rooms`：`roomId -> Room`
- `userChannels`：`uid -> lobby channel(weak_ptr)`

## 构建

依赖要求：

- CMake 3.21+
- C++20 编译器
- Ninja（推荐）
- `asio`、`nlohmann_json`、`GTest`（测试）

默认 `SERVER_FETCH_DEPS=ON`，缺失依赖时自动下载。

```bash
cmake --preset release
cmake --build build
```

## 测试

```bash
cmake --build build --target unit_tests
ctest --test-dir build --output-on-failure
```

## 运行

```bash
./build/server --auth-port 8765 --lobby-port 8766
```

- `auth-port` 处理 `LOGIN/REGISTER/LOGOUT`
- `lobby-port` 处理房间命令
- 两个端口必须不同

## 示例请求

注册：

```json
{"type":1,"uid":""}
```

创建房间：

```json
{"type":0,"uid":"1001","maximumPeople":4}
```

使用 `nc` 快速调试（每条 JSON 后换行）：

```bash
nc 127.0.0.1 8765
nc 127.0.0.1 8766
```