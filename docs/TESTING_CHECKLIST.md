# 测试清单（收敛版）

本文档基于当前实现，收敛为四条主线：

- 协议契约测试（短响应、长响应、枚举稳定性）
- TCP 端到端集成测试（半包、粘包、非法 JSON、断连恢复）
- 并发一致性测试（并发 join/leave、边界容量）
- 阶段行为测试（商店、地图、战斗）

## 0. 统一前提

- 协议 `type` 为数值枚举，不是字符串。
- 成功的 short response 使用 `ShortEnvelope{code,data,message}`。
- 成功的 long response 使用 `LongEnvelope{type,data,pushMessages}`。
- `NoResponseRsp` 表示成功后不发送 direct response，而改走广播。
- 成功响应：`code & SUCCESS != 0`。
- 失败响应：`code & FAIL` 或 `code & ERROR` 至少一个非 0。

## 1. 协议契约测试（P0，必须）

### 1.1 覆盖命令

- 登录域：`REGISTER`、`LOGIN`、`LOGOUT`
- 大厅域：`CREATE_ROOM`、`JOIN_ROOM`、`LEAVE_ROOM`、`LIST_ROOMS`
- 商店域：`SHOP_INIT`、`SHOP_MOVE_CURSOR`、`SHOP_BUY`
- 地图域：`MAP_INIT`、`MAP_MOVE`
- 战斗域：`PLAYER_READY`、`BATTLE_SYNC`、`PLAYER_SHOOT`

### 1.2 快照断言（每个命令都要有）

- short response 顶层字段必须存在：`code`、`message`、`data`
- long response 顶层字段必须存在：`type`、`data`、`pushMessages`
- 字段类型固定：
	- `code` / `type` 为 number
	- `message` 为 string
	- `data` 为 object
	- `pushMessages` 为 array
- `type` 字段必须是 number（非法类型进入错误分支）

### 1.3 空 data 与结构约束

- 对应 `EmptyRsp` 的成功命令，`data` 必须是空对象 `{}`。
- 对应非空 short response 命令，`data` 必须包含预期业务字段。
- 对应 long response 命令，`data` 必须符合当前阶段结构，例如：
	- `SHOP_INIT -> {items, playerInfos}`
	- `MAP_INIT -> {map}`
	- `MAP_SYNC -> {selectStatus}`
	- `BATTLE_WAIT -> {gameFrame, readyCount, totalCount}`
	- `BATTLE_FRAME -> {serverTick, playerEntities, events}`

### 1.4 错误码契约

- 非法 JSON：`SYSTEM_ERROR | DESERIALIZE_FAIL`
- 未知命令：`SERVICE_FAIL | BAD_REQUEST`
- 资源不存在：`SERVICE_FAIL | NOT_FOUND`
- 房间状态冲突：`SERVICE_FAIL | ROOM_STATE_ERROR`

## 2. TCP 端到端集成测试（P0-P1，高优先）

### 2.1 报文边界

- 半包：一条 JSON 分多次发送，服务端应正确拼包。
- 粘包：一次发送多条 JSON（含换行分隔），服务端应逐条处理。
- 空帧：仅发送换行，服务端应忽略并保持连接。
- 超长帧：超过 `MAX_MESSAGE_SIZE`，服务端应拒绝该连接。

### 2.2 输入健壮性

- 非法 JSON（缺失括号、引号错误等）不应导致崩溃。
- 字段缺失/字段类型错误应返回失败 `ShortEnvelope`。
- 多余字段不应影响已定义字段解析。

### 2.3 连接生命周期

- 客户端主动断连后，服务端不崩溃。
- 断连后同 uid 重新登录可恢复（符合当前实现约束）。
- 断连与重连交替场景下，不应出现脏会话或幽灵在线状态。

## 3. 并发一致性测试（P1-P2，高价值）

### 3.1 并发 join/leave

- 多用户并发加入同一房间，最终成员数量与成员列表一致。
- 同一用户快速 join/leave 重复操作，状态不应损坏。
- 并发 leave 后空房间应被及时清理。

### 3.2 边界容量

- `maximumPeople=1/2/N` 边界值行为正确。
- 房间满员时后续 join 必须失败，且不会超卖。
- 并发 join 下仍满足容量上限。

### 3.3 一致性不变量

- 任意时刻用户最多属于一个房间。
- `rooms` 中成员容器大小与成员列表一致。
- 离房后用户 `room_id` 复位。

## 4. 阶段行为测试（P0-P1，高优先）

### 4.1 阶段状态机

- 新建房间初始处于 `LOBBY`。
- `JOIN_ROOM` 只在 `LOBBY` 阶段成功；进入后续阶段后加入应返回 `ROOM_STATE_ERROR`。
- `SET_READY` 只在 `LOBBY` 阶段成功。
- 全员 `SET_READY=true` 后进入 `SHOP`，并广播 `ALL_READY`。
- `SHOP` 阶段允许商店接口；临时测试旁路允许 `PLAYER_READY` 直接开战。
- 首次合法 `MAP_INIT` 后进入 `MAP`。
- `MAP` 阶段未提交节点时，`PLAYER_READY` 必须返回 `ROOM_STATE_ERROR`。
- `MAP` 阶段提交节点后，`PLAYER_READY` 才能进入战斗准备流程。
- `BATTLE` 阶段拒绝商店接口、地图移动和大厅 ready。
- 战斗胜利且当前节点还有后继时回到 `MAP`，随后可以继续 `MAP_MOVE`。
- 战斗失败或最后节点胜利时进入 `END`。
- 离房/登出清理动作在任意阶段都不应破坏房间状态。

### 4.2 商店阶段

- `SHOP_INIT` 返回的 `items.size()` 应等于 `min(当前房间人数, 物品库大小)`。
- 同一房间的商店物品顺序在房间创建后保持稳定，不应每次 `SHOP_INIT` 都重新洗牌。
- `SHOP_MOVE_CURSOR` 对非法 `itemId` 应按当前实现视为取消选择，而不是失败。
- 非 `SHOP` 阶段调用 `SHOP_INIT` / `SHOP_MOVE_CURSOR` / `SHOP_BUY` 应返回 `ROOM_STATE_ERROR`。
- `SHOP_BUY` 对已购买物品返回 `SHOP_ITEM_TAKEN`。
- `SHOP_BUY` 对不可见或不存在的物品返回 `SHOP_INVALID_ITEM`。

### 4.3 地图阶段

- `MAP_INIT` 懒生成共享地图，后续调用返回同一张图。
- `MAP_INIT` 只在 `SHOP` 或 `MAP` 阶段成功；首次成功调用应推进到 `MAP`。
- 第一列必须是唯一根节点，最后一列必须是唯一 `BOSS` 节点。
- 相邻列连接必须满足：无交叉、后一列全覆盖、允许一对多和多对一。
- 首次提交必须从根节点开始。
- 后续提交必须是当前已提交节点的合法后继。
- 只有全员选择同一节点时才会 commit。
- 非 `MAP` 阶段调用 `MAP_MOVE` 应返回 `ROOM_STATE_ERROR`。

### 4.4 战斗阶段

- `PLAYER_READY` 临时允许在 `SHOP` 阶段直接成功；在 `MAP` 阶段仍要求已提交当前地图节点。
- `PLAYER_READY` 未全员就绪时广播 `BATTLE_WAIT`。
- 全员就绪时首个广播应为 `BATTLE_FRAME`，且 `pushMessages` 含 `BATTLE_START`。
- `BATTLE_SYNC` 上报玩家位置和怪物位置；`BATTLE_FRAME` 只同步玩家实体和事件，不包含每帧怪物/子弹实体列表。
- `PLAYER_SHOOT` 生成玩家子弹和 `BULLET_SPAWN`；后续 tick 推进子弹，命中墙/敌人时产生 hit、damage、destroy 事件。
- 非 `BATTLE` 阶段调用 `BATTLE_SYNC` / `PLAYER_SHOOT` 应返回 `ROOM_STATE_ERROR`。
- 战斗结束帧应带 `BATTLE_END`。
- 敌人全部死亡后，若当前地图节点还有后继，下一次 `tick_battle()` 应失败且 `MAP_MOVE` 可继续。
- 玩家全灭后，下一次 `tick_battle()` 应失败，并且房间进入结束态。

## 5. 分层落地建议

### 5.1 单元测试

- `Protocol` 序列化/反序列化与 Envelope 映射
- `Room` 容量与成员变更逻辑
- `Room` 阶段状态机、商店数量规则、地图提交规则、战斗状态推进
- `Server` 业务 API 状态迁移与广播分发

### 5.2 集成测试

- 真实 TCP 连接测试完整命令流
- 覆盖 auth/lobby/shop/map/battle 五个端口
- battle 网络测试覆盖临时直达流程 `LOBBY ready -> PLAYER_READY`；shop/map 网络测试继续覆盖原接口。

### 5.3 稳定性测试

- 并发压力 + 随机断连 + 非法报文混注
- 关注崩溃、死锁、资源泄漏

## 6. 验收门槛（收敛）

- Gate A（必须）：第 1 章全部通过。
- Gate B（发布前）：第 2 章全部通过。
- Gate C（里程碑）：第 3 章与第 4 章核心场景通过且无高优缺陷，尤其不能存在跨阶段绕过。
