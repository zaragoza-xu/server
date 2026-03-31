# 项目结构与接口设计

本文档描述当前代码结构、模块职责以及协议/接口设计，面向开发与维护。

## 1. 总体架构

服务端采用单进程、事件驱动模型：

- `Server` 负责监听端口、接受连接、管理用户与房间
- `Channel` 负责单连接读写与协议分发
- `Protocol` 负责 JSON 协议类型定义与序列化
- `User`/`Room` 为基础领域模型

运行主路径：

1. `main.cpp` 创建 `asio::io_context`
2. 构造 `Server` 并启动 `accept_loop`
3. 每次 accept 新连接创建一个 `Channel`
4. `Channel::run()` 循环读消息并调用 `handle_message`
5. 解析请求后进入对应命令处理函数

## 2. 代码结构

### 2.1 目录

- `include/protocol.h`：协议类型、命令映射、Req/Rsp 定义
- `include/server.h`：服务端核心接口
- `include/channel.h`：连接处理接口
- `include/user.h`：用户模型
- `include/room.h`：房间模型
- `src/server.cpp`：连接接入与状态管理实现
- `src/channel.cpp`：请求处理与响应返回实现
- `src/room.cpp`：房间成员管理实现
- `src/main.cpp`：程序入口

### 2.2 模块职责

#### Protocol

- 定义命令枚举 `CommandType`
- 命令类型使用数值编码（JSON 字段 `type`）
- `Envelope` 统一响应封装：
  - `code`
  - `message`
  - `data`
- Req/Rsp 使用 `NLOHMANN_DEFINE_TYPE_*` 宏减少样板序列化代码

#### Server

- 接收连接并启动 `Channel`
- 用户管理：
  - `register_user`
  - `login_user`
  - `logout_user`
  - `get_user`
- 房间管理：
  - `create_room`
  - `join_room`
  - `leave_room`
  - `list_rooms`
- 房间广播：`broadcast_to_room`

#### Channel

- `run()` 负责 socket 读循环
- `handle_message()` 负责命令分发
- 每个命令独立处理函数：
  - `handle_register`
  - `handle_login`
  - `handle_create_room`
  - `handle_join_room`
  - `handle_list_rooms`
  - `handle_leave_room`

#### User / Room

- `User`：保存 `uid/userName/avatarType/channel/roomId`
- `Room`：保存 `roomId/roomName/maximumPeople/members`

## 3. 协议设计

### 3.1 Envelope

统一报文结构：

```json
{
  "code": 1,
  "message": "ok",
  "data": { ... }
}
```

说明：

- `code`：位掩码状态与细节码（例如 `SUCCESS`、`FAIL|NOT_FOUND`、`ERROR|DESERIALIZE_FAIL`）
- `code`：位掩码状态与细节码（例如 `SUCCESS`、`FAIL|NOT_FOUND`、`ERROR|DESERIALIZE_FAIL`）
- `message`：错误或状态说明
- `data`：业务载荷

### 3.2 命令类型

当前命令：

- `1`：`register`
- `2`：`login`
- `3`：`create_room`
- `4`：`join_room`
- `5`：`leave_room`
- `6`：`list_rooms`
- `7`：`send_message`
- `8`：`heartbeat`
- `100`：`error`

### 3.3 请求/响应模型

主要请求模型：

- `RegisterReq`
- `LoginReq`
- `CreateRoomReq`
- `JoinRoomReq`
- `LeaveRoomReq`
- `ListRoomsReq`
- `SendMessageReq`
- `HeartbeatReq`

主要响应模型：

- `LoginRsp`
- `CreateRoomRsp`
- `JoinRoomRsp`
- `LeaveRoomRsp`（当前注释结构，按需启用）
- `ListRoomsRsp`
- `SendMessagePush`

## 4. 关键接口

### 4.1 Server 接口

- 用户：
  - `register_user(const Protocol::PlayerBasicInfo, std::shared_ptr<Channel>)`
  - `login_user(const std::string&, std::shared_ptr<Channel>)`
  - `logout_user(const std::string&)`
  - `get_user(const std::string&)`
- 房间：
  - `create_room(const std::string&, size_t, std::shared_ptr<User>)`
  - `get_room(int)`
  - `join_room(std::shared_ptr<Room>, std::shared_ptr<User>)`
  - `leave_room(int, const std::string&)`
  - `list_rooms(std::vector<Protocol::RoomInfo>&)`

### 4.2 Channel 接口

- 网络：
  - `run()`
  - `send_message(const std::string&)`
- 分发与处理：
  - `handle_message(std::string&)`
  - `handle_xxx(const json&)`

## 5. 设计取舍与现状

- 已完成：
- 已完成：
  - type 数值化
  - Envelope `code/message/data` 统一
  - 命令处理拆分
  - 协议序列化宏化
- 待完善：
  - `send_message` 命令在分发层未完整打通
  - 文档示例与代码需保持持续同步

## 6. 后续建议

- 为每个命令补单元测试与集成测试
- 补全 `send_message` 全链路（请求、分发、广播）
- 在 CI 中增加协议回归检查（字段名、type 字符串、错误码）
- 在 CI 中增加协议回归检查（字段名、type 数值、错误码位掩码）
- 增加并发场景测试（离房、广播、断连重入）