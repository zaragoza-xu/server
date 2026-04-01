# server

一个基于 ASIO 协程的轻量级 TCP 聊天房间服务端。

## 项目简介

本项目实现了一个 JSON 协议的 TCP 服务端，核心能力包括：

- 用户注册（REGISTER）
- 用户登录（LOGIN）
- 创建房间（CREATE_ROOM）
- 加入房间（JOIN_ROOM）
- 离开房间（LEAVE_ROOM）
- 列出房间（LIST_ROOMS）

当前协议约定：

- 消息类型 `type` 使用字符串（例如 `"login"`、`"create_room"`）
- 信封字段使用 `data` 承载业务数据
- 统一返回结构由 `Envelope` 描述（`code/message/data`）
- `code` 使用位掩码：低位表示成功/失败/系统错误，高位表示具体错误细节

## 目录概览

- `include/` 头文件与协议定义
- `src/` 业务实现
- `scripts/build-release.sh` 一键构建脚本
- `CMakeLists.txt` CMake 构建配置
- `CMakePresets.json` 预设构建配置（release + Ninja + vcpkg）

## 依赖要求

- CMake 3.21+
- C++20 编译器
- Ninja（推荐）
- 库依赖：
  - `asio`
  - `nlohmann_json`
  - `GTest`（仅单元测试需要）

说明：

- 默认开启 `SERVER_FETCH_DEPS=ON`，在本机缺少依赖时，CMake 会自动下载并集成这些库。
- 如果你已经通过系统包管理器或 vcpkg 安装了依赖，可关闭自动下载：

```bash
cmake -S . -B build -DSERVER_FETCH_DEPS=OFF
```

## 构建方式

### 方式一：使用 CMake Preset（推荐）

```bash
cmake --preset release
cmake --build build
```

如果你希望继续使用 vcpkg 工具链 preset：

```bash
cmake --preset release-vcpkg
cmake --build --preset release-vcpkg
```

### 方式二：使用脚本

```bash
./scripts/build-release.sh
```

可透传构建参数，例如并行数：

```bash
./scripts/build-release.sh -j8
```

### 方式三：手动 CMake（不使用 preset）

如果你已自行配置工具链，也可以直接：

```bash
cmake -S . -B build
cmake --build build
```

## 单元测试

构建（默认会构建 `unit_tests`）：

```bash
cmake --preset release
cmake --build build --target unit_tests
```

运行：

```bash
ctest --test-dir build --output-on-failure
```

## 运行

```bash
./build/server
```

服务默认监听端口：`7777`。

## 快速测试

可用 `nc` 连接测试（每条 JSON 末尾换行）：

```bash
nc 127.0.0.1 7777
```