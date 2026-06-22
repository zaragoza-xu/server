# 协议约定

本文档描述当前 TCP JSON 协议、命令枚举、Envelope 语义与实时推送行为。

## 1. 消息格式

- 传输层：TCP 文本帧，每条消息以换行符 `\n` 结尾
- 负载：JSON
- 最大消息长度：`Protocol::MAX_MESSAGE_SIZE = 65536`
- 所有 `type` 字段都使用整型枚举值，不使用字符串命令名

## 2. 命令类型

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
- `BattleRequestType::POSITION_SYNC = 1`（业务语义为战斗位置同步）
- `BattleRequestType::PLAYER_SHOOT = 2`
- `BattleResponseType::BATTLE_WAIT = 0`
- `BattleResponseType::BATTLE_FRAME = 1`
- `BattlePushMessageType::BATTLE_START = 0`
- `BattlePushMessageType::BATTLE_END = 1`

## 3. Envelope 语义

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

## 4. 当前实时推送行为

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
- `POSITION_SYNC` 接收客户端实际玩家位置和本地计算的怪物位置
- 玩家位置以客户端最新上报为准；怪物位置在 `tick_battle()` 中按实体取各客户端最新上报的均值
- `BATTLE_FRAME.data` 当前包含 `serverTick`、`playerEntities`、`events`；不再每帧同步 `enemyEntities` 和 `bulletEntities`
- 敌人/子弹的出生、命中、销毁通过 `events` 广播；敌人生成事件的 `attribute` 包含 `attackCoolDown`
- `PLAYER_SHOOT` 记录射击输入；下一次 tick 生成玩家子弹或结算近战，并通过 `BULLET_SPAWN` / `WEAPON_HIT_ENEMY` 等事件广播
- 子弹在 tick 中推进，命中敌人时广播 hit、damage、destroy 事件，射程耗尽时以 `BULLET_TIMEOUT` 销毁
- 战斗结束的帧会在 `pushMessages` 中附带 `BATTLE_END`

## 5. 当前阶段行为补充

房间阶段状态机：

- 初始阶段为 `LOBBY`
- `LOBBY`：允许建房、加入、离房、房间 ready；全员 ready 后进入 `SHOP`
- `SHOP`：允许 `SHOP_INIT`、`SHOP_MOVE_CURSOR`、`SHOP_BUY`；首次合法 `MAP_INIT` 后进入 `MAP`
- `MAP`：允许 `MAP_INIT`、`MAP_MOVE`；全员提交同一个合法节点后，才允许战斗 ready
- `BATTLE`：允许 `PLAYER_READY` 启动后的 `POSITION_SYNC`、`PLAYER_SHOOT` 和服务端 tick；商店/地图/大厅 ready 等非战斗入口会返回房间状态错误
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
- `POSITION_SYNC`、`PLAYER_SHOOT`、`tick_battle()` 只在 `BATTLE` 阶段成功
- 战斗胜利会清空战斗临时状态；若当前节点还有后继则回到 `MAP`，否则进入 `END`
- 玩家全灭按失败处理，清空战斗临时状态并进入 `END`
