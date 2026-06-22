# server

基于 standalone Asio 协程的轻量级 TCP 房间服务端。服务拆分为登录、大厅、商店、地图、战斗五个端口，共享同一份房间与用户状态。

## 文档

- [架构设计](docs/ARCHITECTURE.md)
- [协议约定](docs/PROTOCOL.md)
- [项目结构](docs/PROJECT_STRUCTURE.md)
- [测试清单](docs/TESTING_CHECKLIST.md)

## 从源码构建

依赖：CMake 3.21+、C++20 编译器、Ninja。缺失的 `asio`、`nlohmann_json`、`GTest` 默认会通过 `FetchContent` 拉取。

```bash
cmake --preset release
cmake --build --preset release
./build/server --config config/server.json
```

## 测试

```bash
cmake --preset debug-tests
cmake --build --preset debug-tests
ctest --preset debug-tests --output-on-failure
```

网络测试可按名称过滤：

```bash
ctest --preset debug-tests -R "network_tests::" --output-on-failure
```

## 发布

生成 release 产物包：

```bash
scripts/package-release.sh
```

解压后运行：

```bash
./bin/server --config config/server.json
```
