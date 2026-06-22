# 压力测试说明

## 依赖

- Python 3.10+
- Bash
- 已启动的服务端，端口需与压测配置一致；仓库主配置 `config/server.json` 当前使用 `22222–22226`
- `configs/*.json` 可以覆盖目标端口；运行本机服务前先确认场景配置与 `config/server.json` 一致
- 可选：`tc` + `netem`（网络注入，需 root/CAP_NET_ADMIN）

## 关键文件

| 文件 | 作用 |
|------|------|
| `run_stress.sh` | 统一入口（local/docker、netem、ramp） |
| `run_stress.py` | Python 压测核心 |
| `scenarios.py` | 场景实现 |
| `orchestrator/` | 阶梯加压、netem、knee 检测 |
| `scripts/netem.sh` | tc/netem 封装 |
| `docker-compose.stress.yml` | 隔离环境 |
| `configs/*.json` | 场景配置 |

## 场景列表

| 场景 | 说明 |
|------|------|
| `auth_register_login` | 认证端口短连接 |
| `lobby_join_hot_room` | 大厅热点房争抢 |
| `e2e_short_conn` | 认证 + 大厅短连接 E2E |
| `battle_sync` | 多房间战斗 sync 负载 |
| `full_flow` | Auth → Lobby → Map → Battle 全流程 |

## 启动服务器

```bash
cmake --preset release && cmake --build --preset release
./build/server --config config/server.json
```

## 运行示例

```bash
# 单场景（默认无网络干扰）
bash tests/stress_tests/run_stress.sh auth_register_login --local

# 快速冒烟（30s）
bash tests/stress_tests/run_stress.sh e2e_short_conn --local --quick

# 阶梯加压 + 压力点报告
python3 tests/stress_tests/run_stress.py \
  --config tests/stress_tests/configs/auth_register_login.json \
  --ramp --ramp-config tests/stress_tests/configs/ramp_quick.yaml

# 弱网注入
bash tests/stress_tests/run_stress.sh auth_register_login --local \
  --netem "delay 50ms 10ms loss 0.5%"

# Docker 隔离环境
bash tests/stress_tests/run_stress.sh battle_sync --docker --quick

# 全部场景
bash tests/stress_tests/run_all.sh --quick
```

## CLI 参数（run_stress.py / run_stress.sh）

| 参数 | 默认 | 说明 |
|------|------|------|
| `--netem` | 空 | netem 规则，空=无干扰 |
| `--ramp` | 关 | 启用阶梯加压与降压恢复 |
| `--ramp-config` | ramp_default.yaml | 阶梯配置 |
| `--duration-seconds` | 配置值 | 覆盖单次运行时长 |
| `--concurrency` | 配置值 | 覆盖并发 |
| `--max-concurrency` | 无 | 阶梯并发上限 |

## 输出

每次运行在 `output/<scenario>/<timestamp>_<single|ramp>_<net-normal|net-weak>/` 生成：

- `result.json` / `summary.txt` — 汇总指标
- `charts.html` — 关键指标列表
- **ramp 模式额外：**
  - `steps.csv` / `timeseries.csv` — 阶梯与时间序列数据
  - `report.html` — 完整报告
  - `charts/timeseries_combined.svg` — **单张归一化折线图**（吞吐/P95/P99/成功率，图例区分颜色；橙色虚线为压力点）

`latest/` 始终指向最近一次结果。

## 压力点判定

阶梯模式下 `knee_analysis` 字段包含：

- `recommended_safe_concurrency` — 最后未触发拐点的并发
- `first_knee_concurrency` — 首个压力点
- `bottleneck_type` — throughput_plateau / latency_spike / unexpected_errors / connect_failures

触发条件：吞吐增长 <10%、P99 环比 >1.5x、非预期错误率超阈、建连失败。

## 网络控制

`scripts/netem.sh` 封装 tc/netem：

```bash
bash tests/stress_tests/scripts/netem.sh apply --rule "delay 100ms 20ms loss 0.5%"
bash tests/stress_tests/scripts/netem.sh clear
```

默认（`--netem` 为空）会清除规则，不对流量施加干扰。
