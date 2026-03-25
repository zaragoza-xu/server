# TCP 服务器功能说明

## 新增功能

该 TCP 服务器现在支持以下功能：

### 1. 用户登录 (LOGIN)
**命令**：
```json
{
  "type": 0,
  "username": "user1"
}
```

**响应**：
```json
{
  "type": 0,
  "username": "user1",
  "room_id": -1,
  "room_name": "",
  "content": "",
  "error_message": ""
}
```

### 2. 创建房间 (CREATE_ROOM)
用户必须先登录才能创建房间。

**命令**：
```json
{
  "type": 1,
  "room_name": "room1"
}
```

**响应**：
```json
{
  "type": 1,
  "room_id": 1,
  "room_name": "room1",
  "error_message": ""
}
```

### 3. 加入房间 (JOIN_ROOM)
用户必须先登录才能加入房间。

**命令**：
```json
{
  "type": 2,
  "room_id": 1
}
```

**响应**：
```json
{
  "type": 2,
  "room_id": 1,
  "room_name": "room1",
  "error_message": ""
}
```

### 4. 离开房间 (LEAVE_ROOM)
**命令**：
```json
{
  "type": 3
}
```

**响应**：
```json
{
  "type": 3,
  "room_id": 1,
  "error_message": ""
}
```

### 5. 列出房间 (LIST_ROOMS)
**命令**：
```json
{
  "type": 4
}
```

**响应**：
```json
{
  "type": 4,
  "content": "2",
  "error_message": ""
}
```
控制台会输出详细的房间信息。

### 6. 发送房间消息 (SEND_MESSAGE)
消息将广播给房间内的所有用户。

**命令**：
```json
{
  "type": 5,
  "content": "Hello World"
}
```

**广播响应** (发送给房间内所有用户)：
```json
{
  "type": 5,
  "username": "user1",
  "room_id": 1,
  "content": "Hello World",
  "error_message": ""
}
```

## 测试示例

使用 `nc` 或 `telnet` 连接到服务器 (localhost:7777)：

```bash
# 终端 1
nc localhost 7777

# 输入登录命令
{"type": 0, "username": "alice"}

# 创建房间
{"type": 1, "room_name": "chat_room"}

# 发送消息
{"type": 5, "content": "Hello everyone!"}
```

```bash
# 终端 2
nc localhost 7777

# 登录另一个用户
{"type": 0, "username": "bob"}

# 加入房间
{"type": 2, "room_id": 1}

# 接收来自 alice 的消息和发送自己的消息
{"type": 5, "content": "Hi alice!"}
```

## 协议细节

- 所有通信使用 JSON 格式
- 消息必须以 `\n` 结尾
- 错误通过 `error_message` 字段返回，类型为 100
- 房间 ID 自动分配，从 1 开始递增
- 空房间会自动删除

## 命令类型值

| 类型名 | 值 |
|--------|-----|
| LOGIN | 0 |
| CREATE_ROOM | 1 |
| JOIN_ROOM | 2 |
| LEAVE_ROOM | 3 |
| LIST_ROOMS | 4 |
| SEND_MESSAGE | 5 |
| ERROR | 100 |
