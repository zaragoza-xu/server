# 项目结构与接口设计

本文档描述当前代码实现的真实架构与协议约束。

## 1. 总体架构

服务端是单进程、事件驱动模型：

- `Server`：监听端口、维护用户/房间状态、执行业务命令
- `Channel`：单连接收发与消息边界处理
- `Protocol`：协议类型与 JSON 序列化定义
- `User` / `Room`：领域实体

启动路径：

1. `main.cpp` 创建 `asio::io_context`
2. 创建共享 `ServerState`
3. 启动两个服务实例：
   - `LoginServer(auth-port)`
   - `HomeServer(lobby-port)`
4. `Server` 构造时启动 `accept_loop`
5. 每个连接创建一个 `Channel`，按行分帧读取 JSON，并调用 `server->dispatch_request`

## 2. 模块职责

### 2.1 Protocol

- 定义命令枚举：
  - `LoginRequestType`
  - `HomeRequestType`
- 定义错误码位掩码 `Protocol::Code`
- 定义统一响应 `Envelope{code, message, data}`
- 定义请求/响应结构体与 JSON 宏
- `Req/Rsp` 基类已删除，空响应使用 `EmptyRsp`

### 2.2 Server

- `Server` 作为基类，提供业务 API：
  - `register_user`
  - `login_user`
  - `logout_user`
  - `create_room`
  - `join_room`
  - `leave_room`
  - `list_rooms`
- `LoginServer` 和 `HomeServer` 通过命令表覆盖 `dispatch_request`
- 命令分发采用“普通函数指针 + 模板桥接”模式：
  - `DispatchFn = Envelope (*)(Server&, const json&)`
  - `dispatch_entry<Req, Rsp, Method>`

### 2.3 Channel

- `run()` 进行读循环
- 使用 `\n` 作为消息分隔符
- 校验最大长度，处理空帧和超长帧
- `handle_message()` 仅负责：
  - 解析 JSON
  - 调用 `server->dispatch_request`
  - 返回统一 Envelope

## 3. 协议契约

### 3.1 传输层

- TCP 文本帧
- 一条 JSON 一行（以换行符结尾）
- 最大消息长度：`65536`

### 3.2 命令枚举（type）

`type` 为枚举数值：

- 登录域：`LOGIN=0`、`REGISTER=1`、`LOGOUT=2`、`ERROR=100`
- 大厅域：
  - `CREATE_ROOM=0`
  - `JOIN_ROOM=1`
  - `LEAVE_ROOM=2`
  - `LIST_ROOMS=3`
  - `SEND_MESSAGE=4`（预留）
  - `HEARTBEAT=5`（历史保留值，当前未实现）
  - `EDIT_PROFILE=6`（预留）
  - `ERROR=100`

### 3.3 Envelope 语义

```json
{
  "code": 1,
  "message": "ok",
  "data": {}
}
```

- `code`：状态与细节位掩码
- `message`：由 `Envelope::map_message_from_code` 生成
- `data`：
  - 成功 + 有响应体：序列化响应结构
  - 成功 + 空响应（`EmptyRsp`）：空对象
  - 失败：空对象

## 4. 当前服务接口

`Server` 公开业务接口签名如下：

- `int register_user(const Protocol::RegisterReq&, Protocol::RegisterRsp&)`
- `int login_user(const Protocol::LoginReq&, Protocol::LoginRsp&)`
- `int logout_user(const Protocol::LogoutReq&, Protocol::EmptyRsp&)`
- `int create_room(const Protocol::CreateRoomReq&, Protocol::CreateRoomRsp&)`
- `int join_room(const Protocol::JoinRoomReq&, Protocol::JoinRoomRsp&)`
- `int leave_room(const Protocol::LeaveRoomReq&, Protocol::EmptyRsp&)`
- `int list_rooms(const Protocol::ListRoomsReq&, Protocol::ListRoomsRsp&)`
- `int logout_user(const Protocol::LogoutReq &, Protocol::EmptyRsp &);`

说明：当前 `Channel` 不再保留旧的 `on_*` typed handlers 路径，统一走 `dispatch_request`。

## 5. 并发与状态

- 用户、房间、用户信息分别由互斥锁保护：
  - `usersMutex`
  - `roomsMutex`
  - `userInfosMutex`
- `LoginServer` 与 `HomeServer` 共享同一个 `ServerState`

## 6. 已实现与预留项

已实现：

- 登录/注册/登出
- 创建房间/加入房间/离开房间/列房间

预留未打通：

- `SEND_MESSAGE`
- `EDIT_PROFILE`

## 7. 维护约定

- 任何协议字段、枚举值、接口签名变更，必须同步更新：
  - `README.md`
  - `docs/ARCHITECTURE.md`
  - `tests/unit_tests.cpp`