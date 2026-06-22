# 项目结构

本文档列出当前仓库主要源码、配置、测试和脚本目录职责。

## 头文件

- `include/types.h`：基础协议枚举、错误码、`RoomInfo`、`MapNode` 等共享类型
- `include/protocol.h`：登录/大厅/商店/地图协议与 Envelope 定义
- `include/battle.h`：战斗实体、战斗请求/响应、战斗事件 DTO
- `include/server.h`：`Server` 基类和五个服务子类
- `include/room.h`：房间聚合状态，包含商店、地图、战斗子状态
- `include/channel.h`：连接读写与消息处理接口

## 源码

- `src/server.cpp`：登录/大厅/商店/地图服务逻辑和广播
- `src/battle_server.cpp`：战斗服务分发与 tick 广播逻辑
- `src/room.cpp`：房间阶段迁移、商店和地图状态逻辑
- `src/battle_room.cpp`：战斗启动、同步、tick 推进和结算
- `src/map.cpp`：地图列生成和无交叉路径连接逻辑
- `src/channel.cpp`：按行分帧、JSON 解析、请求分发
- `src/main.cpp`：配置加载与五个服务启动

## 配置

- `config/server.json`：端口配置
- `config/shop_catalog.json`：商店物品目录
- `config/battle_config.json`：战斗数值、武器、敌人与刷怪配置
- `config/wiki-registry.yaml`：飞书模块到代码文件范围的映射
- `config/message_aliases.yaml`：接口文档对齐时使用的消息别名

## 测试

- `tests/unit_tests/`：单元测试目录（`*_tests.cpp`，主要覆盖房间/协议路由/错误码等纯逻辑）
- `tests/network_tests/`：网络与集成测试目录（TCP 分帧、端到端链路、多服务协同）
- `tests/stress_tests/`：压测脚本与场景

## 文档与脚本

- `docs/ARCHITECTURE.md`：架构、运行时行为和维护约定
- `docs/PROTOCOL.md`：协议、Envelope 和实时推送语义
- `docs/TESTING_CHECKLIST.md`：测试检查清单
- `scripts/build-release.sh`：release 构建脚本
- `scripts/package-release.sh`：release 产物打包脚本
- `scripts/push_temp_deploy.sh`：临时部署分支同步脚本
