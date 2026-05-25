# 压力测试说明

## 依赖与安装

本目录脚本主要使用 Python 标准库；通常不需要额外安装 Python 包。

### 必需依赖

- Python 3.10+（建议 3.10 及以上）
- Bash（用于 `run_all.sh`、`ci_quick.sh`）
- 已启动的服务端进程（默认端口 `8765/8766`）

### 检查环境

```bash
python3 --version
bash --version
```

### Python 安装

Linux（Ubuntu/Debian）：

```bash
sudo apt-get update
sudo apt-get install -y python3 python3-venv
```


## 关键文件功能

- `tests/stress_tests/run_stress.py`：压测入口，读取场景配置并执行，输出结果文件。
- `tests/stress_tests/scenarios.py`：场景实现（`auth_register_login`、`lobby_join_hot_room`、`e2e_short_conn`）。
- `tests/stress_tests/client.py`：短连接 TCP 客户端（`connect -> send -> recv -> close`）。
- `tests/stress_tests/metrics.py`：指标统计与阈值判定，按分类输出 `result.json`、`summary.txt` 并生成图表文件。
- `tests/stress_tests/configs/*.json`：场景配置（目标端口、并发、时长、阈值）。
- `tests/stress_tests/ci_quick.sh`：快速门禁脚本（跑快速场景并可选对比基线）。
- `tests/stress_tests/compare_results.py`：基线与当前结果对比（p95 与吞吐退化检查）。

## 执行测试

在仓库根目录执行：

当前压测场景主要覆盖认证与大厅短连接链路，不覆盖 `SHOP -> MAP -> BATTLE` 阶段状态机；阶段流由单元测试和网络集成测试覆盖。若新增商店/地图/战斗压测场景，必须按业务前置流程推进：`LOBBY ready -> SHOP -> MAP_INIT -> MAP_MOVE -> PLAYER_READY`。

执行模型：

- 单机多线程 + 多协程（每个线程一个 asyncio 事件循环）
- 总并发由 `configs/*.json` 的 `concurrency` 控制
- 线程数由 `configs/*.json` 的 `threads` 控制（也可用 `--threads` 覆盖）

## 启动服务器

```bash
./build/server --auth-port 8765 --lobby-port 8766
```

### 运行全部场景

```bash
bash tests/stress_tests/run_all.sh
```

指定线程数运行：

```bash
bash tests/stress_tests/run_all.sh --threads 8
```

说明：

- 会依次执行 3 个场景，不会因为中途失败而提前退出。
- 脚本末尾会输出失败项汇总（场景名 + 退出码）。
- 若存在失败项，脚本最终返回非 0 退出码；全部通过则返回 0。

### 1) auth_register_login

```bash
python3 tests/stress_tests/run_stress.py \
  --config tests/stress_tests/configs/auth_register_login.json
```

### 2) lobby_join_hot_room

```bash
python3 tests/stress_tests/run_stress.py \
  --config tests/stress_tests/configs/lobby_join_hot_room.json
```

说明：该场景默认通过周期性发送 `LIST_ROOMS`（`type = 3`，间隔由 `keepalive_interval_seconds` 控制，默认 5 秒）维持会话在线，并减少超时导致的 `NOT_FOUND` 错误。

### 3) e2e_short_conn

```bash
python3 tests/stress_tests/run_stress.py \
  --config tests/stress_tests/configs/e2e_short_conn.json
```

### 运行快速门禁

```bash
bash tests/stress_tests/ci_quick.sh
```

## 输出目录：

- 每次执行都会在场景目录下自动创建时间戳子目录：
  - `tests/stress_tests/output/auth_register_login/<timestamp>/`
  - `tests/stress_tests/output/lobby_join_hot_room/<timestamp>/`
  - `tests/stress_tests/output/e2e_short_conn/<timestamp>/`
- 同时会更新 `latest/` 目录，指向最近一次执行结果：
  - `tests/stress_tests/output/<scenario>/latest/`

## 查看结果

- 单次运行目录下包含：
  - `result.json`
  - `summary.txt`
  - `charts.svg`
  - `charts.html`
- 查看最近一次结果：
  - `tests/stress_tests/output/<scenario>/latest/summary.txt`
  - `tests/stress_tests/output/<scenario>/latest/charts.html`


`summary.txt` 按分类展示字段：

- `[吞吐与请求]`
  - `requests_total(总请求数)`：本次压测中记录到的请求总量（成功+失败）。
  - `throughput_rps(吞吐_每秒请求数)`：平均每秒处理请求数，计算方式为 `requests_total / runtime_seconds`。

- `[延迟]`
  - `latency_e2e_p50_ms(端到端_50分位_毫秒)`：请求端到端总耗时的中位数，反映典型请求时延。
  - `latency_e2e_p95_ms(端到端_95分位_毫秒)`：95% 请求不超过的端到端耗时，反映高分位时延。
  - `latency_e2e_p99_ms(端到端_99分位_毫秒)`：99% 请求不超过的端到端耗时，反映尾延迟。
  - `latency_connect_p95_ms(建连_95分位_毫秒)`：TCP 建连阶段 p95，用于判断建连是否成为瓶颈。
  - `latency_send_p95_ms(发送_95分位_毫秒)`：客户端发送请求帧阶段 p95，通常用于观察发送侧阻塞。
  - `latency_ttfb_p95_ms(首字节等待_95分位_毫秒)`：发送完成到收到响应首字节阶段 p95，常用于判断服务端处理+网络返回等待。
  - `latency_recv_p95_ms(接收_95分位_毫秒)`：客户端接收完整响应阶段 p95，用于观察回包读取阶段耗时。

- `[成功率与错误]`
  - `success_rate(成功率)`：返回 `code == 1` 的请求占比。
  - `error_codes(失败错误码分布)`：失败请求分布，按位输出错误码、错误数量和错误码含义。格式示例：`{code=66, count=12, meaning=FAIL|BAD_REQUEST; code=130, count=3, meaning=FAIL|NOT_FOUND}`。
  - `expected_error_codes(预期错误码分布)`：预期失败请求分布（默认指携带 `FAIL` 位且不携带 `ERROR` 位），输出格式与 `error_codes` 相同。
  - `unexpected_error_codes(非预期错误码分布)`：非预期失败请求分布（默认指携带 `ERROR` 位，或未知状态组合），输出格式与 `error_codes` 相同。

- `[连接稳定性]`
  - `connect_failures(建连失败次数)`：TCP 建连失败总次数。
  - `connect_p95_ms(建连95分位耗时_毫秒)`：建连耗时 p95（与延迟分类中的建连 p95 含义一致）。
  - `close_failures(关闭连接失败次数)`：客户端主动关闭连接失败次数。

- `[场景质量]`
  - `custom_counters(场景自定义计数)`：场景内部定义的业务计数器（例如 `flow_success`、`state_violations`）。
  - `threshold_passed(是否通过阈值门禁)`：该次执行是否通过配置中的全部阈值检查。
  - `threshold_checks(阈值检查详情)`：逐条阈值检查结果列表，每项包含：
    - `name`：阈值项名称。
    - `actual(实际值)`：本次运行观测值（可能是数字或结构化对象）。
    - `expected(预期值)`：配置中的目标阈值。
    - `passed(是否通过)`：该阈值项是否通过。
  - 当前支持的阈值项说明：
    - `max_unexpected_error_rate`：非预期错误率上限。非预期错误指携带 `ERROR` 位（或未知状态组合）的失败请求。
    - `min_flow_success_rate`：流程成功率下限（适用于端到端流程类场景）。
    - `max_p99_latency_ms`：端到端 `p99` 延迟上限（毫秒）。
    - `max_connect_failures`：建连失败次数上限。
    - `max_state_violation_count`：场景一致性违规计数上限（例如 `state_violations`）。
