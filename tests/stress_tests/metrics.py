import json
import math
import time
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional

from orchestrator.knee_detector import bottleneck_label


CODE_BIT_FLAGS = [
    (1, "SUCCESS"),
    (2, "FAIL"),
    (4, "ERROR"),
    (8, "TIME_OUT"),
    (16, "DESERIALIZE_FAIL"),
    (32, "CONNECTION_ERROR"),
    (64, "BAD_REQUEST"),
    (128, "NOT_FOUND"),
    (256, "ROOM_STATE_ERROR"),
]

FAIL_MASK = 1 << 1
ERROR_MASK = 1 << 2


def _quantile(sorted_values: List[float], q: float) -> float:
    if not sorted_values:
        return 0.0
    idx = (len(sorted_values) - 1) * q
    lo = math.floor(idx)
    hi = math.ceil(idx)
    if lo == hi:
        return sorted_values[lo]
    return sorted_values[lo] + (sorted_values[hi] - sorted_values[lo]) * (idx - lo)


def _is_unexpected_error_code(code: int) -> bool:
    # Policy: any code carrying ERROR bit is unexpected; FAIL-only codes are expected business failures.
    return (code & ERROR_MASK) != 0


def _decode_code_bits(code: int) -> List[str]:
    labels: List[str] = []
    for mask, name in CODE_BIT_FLAGS:
        if (code & mask) != 0:
            labels.append(name)
    if labels:
        return labels
    return ["NONE"]


def _format_error_distribution(error_codes: Dict[str, int]) -> str:
    if not error_codes:
        return "{}"
    parts: List[str] = []
    for code_str, count in sorted(error_codes.items(), key=lambda item: int(item[0])):
        code_int = int(code_str)
        meaning = "|".join(_decode_code_bits(code_int))
        parts.append(f"code={code_int:b}, count={int(count)}, meaning={meaning}")
    return "{" + "; ".join(parts) + "}"


def _split_error_codes_by_expectation(error_codes: Dict[str, int]) -> Dict[str, Dict[str, int]]:
    expected: Dict[str, int] = {}
    unexpected: Dict[str, int] = {}
    for code_str, count in error_codes.items():
        code_int = int(code_str)
        if _is_unexpected_error_code(code_int):
            unexpected[code_str] = int(count)
        elif (code_int & FAIL_MASK) != 0:
            expected[code_str] = int(count)
        else:
            # Unknown status shape: treat conservatively as unexpected.
            unexpected[code_str] = int(count)
    return {
        "expected": expected,
        "unexpected": unexpected,
    }


@dataclass
class RunMetrics:
    started_at: float = field(default_factory=time.time)
    ended_at: float = 0.0
    total_requests: int = 0
    success_requests: int = 0
    failed_requests: int = 0
    latency_e2e_ms: List[float] = field(default_factory=list)
    latency_connect_ms: List[float] = field(default_factory=list)
    latency_send_ms: List[float] = field(default_factory=list)
    latency_ttfb_ms: List[float] = field(default_factory=list)
    latency_recv_ms: List[float] = field(default_factory=list)
    connect_latency_ms: List[float] = field(default_factory=list)
    error_codes: Counter = field(default_factory=Counter)
    connect_attempts: int = 0
    connect_failures: int = 0
    close_attempts: int = 0
    close_failures: int = 0
    custom_counters: Counter = field(default_factory=Counter)

    def add(
        self,
        response_code: int,
        connect_ok: bool,
        send_ok: bool,
        ttfb_ok: bool,
        recv_ok: bool,
        close_ok: bool,
        connect_ms: float,
        send_ms: float,
        ttfb_ms: float,
        recv_ms: float,
        e2e_ms: float,
    ) -> None:
        self.total_requests += 1
        self.latency_e2e_ms.append(e2e_ms)
        self.connect_attempts += 1
        if connect_ok:
            self.latency_connect_ms.append(connect_ms)
            self.connect_latency_ms.append(connect_ms)
            self.close_attempts += 1
            if not close_ok:
                self.close_failures += 1
        else:
            self.connect_failures += 1
        if send_ok:
            self.latency_send_ms.append(send_ms)
        if ttfb_ok:
            self.latency_ttfb_ms.append(ttfb_ms)
        if recv_ok:
            self.latency_recv_ms.append(recv_ms)
        if response_code == 1:
            self.success_requests += 1
        else:
            self.failed_requests += 1
            self.error_codes[str(response_code)] += 1

    def finalize(self) -> None:
        self.ended_at = time.time()

    def count(self, key: str, n: int = 1) -> None:
        self.custom_counters[key] += n

    def merge_from(self, other: "RunMetrics") -> None:
        self.started_at = min(self.started_at, other.started_at)
        self.ended_at = max(self.ended_at, other.ended_at)
        self.total_requests += other.total_requests
        self.success_requests += other.success_requests
        self.failed_requests += other.failed_requests
        self.latency_e2e_ms.extend(other.latency_e2e_ms)
        self.latency_connect_ms.extend(other.latency_connect_ms)
        self.latency_send_ms.extend(other.latency_send_ms)
        self.latency_ttfb_ms.extend(other.latency_ttfb_ms)
        self.latency_recv_ms.extend(other.latency_recv_ms)
        self.connect_latency_ms.extend(other.connect_latency_ms)
        self.error_codes.update(other.error_codes)
        self.connect_attempts += other.connect_attempts
        self.connect_failures += other.connect_failures
        self.close_attempts += other.close_attempts
        self.close_failures += other.close_failures
        self.custom_counters.update(other.custom_counters)

    def to_report(self, scenario_name: str) -> Dict:
        runtime_s = max(self.ended_at - self.started_at, 1e-9)
        e2e_sorted = sorted(self.latency_e2e_ms)
        connect_sorted = sorted(self.connect_latency_ms)
        send_sorted = sorted(self.latency_send_ms)
        ttfb_sorted = sorted(self.latency_ttfb_ms)
        recv_sorted = sorted(self.latency_recv_ms)
        conn_sorted = sorted(self.connect_latency_ms)
        requests = {
            "total": self.total_requests,
            "success": self.success_requests,
            "failed": self.failed_requests,
            "success_rate": self.success_requests / self.total_requests if self.total_requests else 0.0,
            "throughput_rps": self.total_requests / runtime_s,
        }
        latency_ms = {
            "p50": _quantile(e2e_sorted, 0.50),
            "p95": _quantile(e2e_sorted, 0.95),
            "p99": _quantile(e2e_sorted, 0.99),
        }
        latency_breakdown_ms = {
            "e2e": latency_ms,
            "connect": {
                "p50": _quantile(connect_sorted, 0.50),
                "p95": _quantile(connect_sorted, 0.95),
                "p99": _quantile(connect_sorted, 0.99),
            },
            "send": {
                "p50": _quantile(send_sorted, 0.50),
                "p95": _quantile(send_sorted, 0.95),
                "p99": _quantile(send_sorted, 0.99),
            },
            "ttfb": {
                "p50": _quantile(ttfb_sorted, 0.50),
                "p95": _quantile(ttfb_sorted, 0.95),
                "p99": _quantile(ttfb_sorted, 0.99),
            },
            "recv": {
                "p50": _quantile(recv_sorted, 0.50),
                "p95": _quantile(recv_sorted, 0.95),
                "p99": _quantile(recv_sorted, 0.99),
            },
        }
        connection = {
            "connect_attempts": self.connect_attempts,
            "connect_failures": self.connect_failures,
            "connect_p95_ms": _quantile(conn_sorted, 0.95),
            "close_attempts": self.close_attempts,
            "close_failures": self.close_failures,
        }
        error_codes = dict(self.error_codes)
        custom_counters = dict(self.custom_counters)
        return {
            "scenario": scenario_name,
            "started_at_epoch": self.started_at,
            "ended_at_epoch": self.ended_at,
            "runtime_seconds": runtime_s,
            "requests": requests,
            "latency_ms": latency_ms,
            "latency_breakdown_ms": latency_breakdown_ms,
            "connection": connection,
            "error_codes": error_codes,
            "custom_counters": custom_counters,
            "performance": {
                "throughput_rps": requests["throughput_rps"],
                "latency_ms": latency_breakdown_ms,
            },
            "stability": {
                "success_rate": requests["success_rate"],
                "connection": connection,
            },
            "quality": {
                "error_codes": error_codes,
                "custom_counters": custom_counters,
            },
        }


def evaluate_thresholds(report: Dict, thresholds: Dict) -> Dict:
    checks = []
    req = report["requests"]
    lat = report["latency_ms"]
    lat_breakdown = report.get("latency_breakdown_ms", {})
    conn = report["connection"]
    counters = report.get("custom_counters", {})

    if "max_unexpected_error_rate" in thresholds:
        split_codes = _split_error_codes_by_expectation(report.get("error_codes", {}))
        all_failures = int(req.get("failed", 0))
        unexpected_failures = sum(int(v) for v in split_codes["unexpected"].values())
        err_rate = (unexpected_failures / float(req["total"])) if req.get("total") else 0.0
        limit = float(thresholds["max_unexpected_error_rate"])
        checks.append(
            (
                "max_unexpected_error_rate",
                err_rate <= limit,
                {
                    "unexpected_error_rate": err_rate,
                    "unexpected_failures": unexpected_failures,
                    "all_failures": all_failures,
                },
                limit,
            )
        )
    if "min_flow_success_rate" in thresholds:
        flow_success = int(counters.get("flow_success", 0))
        flow_failures = sum(int(v) for k, v in counters.items() if str(k).startswith("flow_fail_"))
        flow_total = flow_success + flow_failures
        value = (flow_success / float(flow_total)) if flow_total > 0 else 0.0
        limit = float(thresholds["min_flow_success_rate"])
        checks.append(("min_flow_success_rate", value >= limit, value, limit))
    if "max_p99_latency_ms" in thresholds:
        value = float(lat["p99"])
        limit = float(thresholds["max_p99_latency_ms"])
        checks.append(("max_p99_latency_ms", value <= limit, value, limit))
    if "max_connect_failures" in thresholds:
        value = int(conn["connect_failures"])
        limit = int(thresholds["max_connect_failures"])
        checks.append(("max_connect_failures", value <= limit, value, limit))
    if "max_state_violation_count" in thresholds:
        value = int(counters.get("state_violations", 0))
        limit = int(thresholds["max_state_violation_count"])
        checks.append(("max_state_violation_count", value <= limit, value, limit))

    return {
        "passed": all(c[1] for c in checks) if checks else True,
        "checks": [
            {
                "name": name,
                "passed": passed,
                "actual": actual,
                "expected": expected,
            }
            for name, passed, actual, expected in checks
        ],
    }


def write_outputs(report: Dict, output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    json_path = output_dir / "result.json"
    txt_path = output_dir / "summary.txt"
    json_path.write_text(json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")

    req = report["requests"]
    lat = report["latency_ms"]
    lat_breakdown = report.get("latency_breakdown_ms", {})
    conn = report["connection"]
    runner = report.get("runner", {})
    error_codes = report.get("error_codes", {})
    split_codes = _split_error_codes_by_expectation(error_codes)
    lines = [
        f"scenario: {report['scenario']}",
        f"runtime_seconds: {report['runtime_seconds']:.2f}",
        "",
        "[执行器]",
        f"mode(运行模式): {runner.get('mode', 'single')}",
        f"thread_count(线程数): {runner.get('thread_count', 1)}",
        f"total_concurrency(总并发): {runner.get('total_concurrency', req['total'])}",
        f"per_thread_concurrency(每线程并发): {runner.get('per_thread_concurrency', [])}",
    ]
    if runner.get("netem_rule"):
        lines.append(f"netem_rule(网络规则): {runner['netem_rule']}")
    if runner.get("output_tag"):
        lines.append(f"output_tag(输出标签): {runner['output_tag']}")
    if report.get("knee_analysis"):
        ka = report["knee_analysis"]
        lines.extend(
            [
                "",
                "[压力点分析]",
                f"recommended_safe_concurrency(推荐安全并发): {ka.get('recommended_safe_concurrency')}",
                f"first_knee_concurrency(首个压力点并发): {ka.get('first_knee_concurrency')}",
                f"bottleneck_type(瓶颈类型): {ka.get('bottleneck_type_zh', bottleneck_label(str(ka.get('bottleneck_type', ''))))}",
                f"recovery_ok(降压恢复): {ka.get('recovery_ok')}",
            ]
        )
        for knee in ka.get("knees", []):
            reasons_zh = knee.get("reasons_zh") or [
                bottleneck_label(r) for r in knee.get("reasons", [])
            ]
            lines.append(
                f"  knee: concurrency={knee.get('concurrency')} "
                f"reasons(原因)={reasons_zh} "
                f"rps={knee.get('throughput_rps', 0):.2f} p99={knee.get('p99_ms', 0):.2f}ms"
            )
    lines.extend(
        [
        "",
        "[吞吐与请求]",
        f"requests_total(总请求数): {req['total']}",
        f"throughput_rps(吞吐_每秒请求数): {req['throughput_rps']:.2f}",
        "",
        "[延迟]",
        f"latency_e2e_p50_ms(端到端_50分位_毫秒): {lat.get('p50', 0.0):.2f}",
        f"latency_e2e_p95_ms(端到端_95分位_毫秒): {lat.get('p95', 0.0):.2f}",
        f"latency_e2e_p99_ms(端到端_99分位_毫秒): {lat.get('p99', 0.0):.2f}",
        f"latency_connect_p95_ms(建连_95分位_毫秒): {lat_breakdown.get('connect', {}).get('p95', 0.0):.2f}",
        f"latency_send_p95_ms(发送_95分位_毫秒): {lat_breakdown.get('send', {}).get('p95', 0.0):.2f}",
        f"latency_ttfb_p95_ms(首字节等待_95分位_毫秒): {lat_breakdown.get('ttfb', {}).get('p95', 0.0):.2f}",
        f"latency_recv_p95_ms(接收_95分位_毫秒): {lat_breakdown.get('recv', {}).get('p95', 0.0):.2f}",
        "",
        "[成功率与错误]",
        f"success_rate(成功率): {req['success_rate']:.4f}",
        f"error_codes(失败错误码分布): {_format_error_distribution(error_codes)}",
        f"expected_error_codes(预期错误码分布): {_format_error_distribution(split_codes['expected'])}",
        f"unexpected_error_codes(非预期错误码分布): {_format_error_distribution(split_codes['unexpected'])}",
        "",
        "[连接稳定性]",
        f"connect_failures(建连失败次数): {conn['connect_failures']}",
        f"connect_p95_ms(建连95分位耗时_毫秒): {conn['connect_p95_ms']:.2f}",
        f"close_failures(关闭连接失败次数): {conn['close_failures']}",
        "",
        "[场景质量]",
        f"custom_counters(场景自定义计数): {report.get('custom_counters', {})}",
    ]
    )
    if "threshold_result" in report:
        threshold_result = report["threshold_result"]
        lines.append(f"threshold_passed(是否通过阈值门禁): {threshold_result.get('passed', True)}")
        lines.append("threshold_checks(阈值检查详情):")
        checks = threshold_result.get("checks", [])
        if not checks:
            lines.append("  - 无阈值检查项")
        else:
            for c in checks:
                lines.append(
                    "  - "
                    f"{c.get('name', 'unknown')} | "
                    f"actual(实际值)={c.get('actual')} | "
                    f"expected(预期值)={c.get('expected')} | "
                    f"passed(是否通过)={c.get('passed')}"
                )
    txt_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    _write_summary_html(report, output_dir)
    if report.get("ramp_steps"):
        _write_ramp_artifacts(report, output_dir)


def _summary_metric_items(report: Dict) -> List[tuple[str, str]]:
    req = report["requests"]
    lat = report["latency_ms"]
    conn = report["connection"]
    return [
        ("吞吐（req/s）", f"{float(req['throughput_rps']):.2f}"),
        ("成功率（%）", f"{float(req['success_rate']) * 100:.2f}"),
        ("P50 延迟（ms）", f"{float(lat.get('p50', 0)):.2f}"),
        ("P95 延迟（ms）", f"{float(lat.get('p95', 0)):.2f}"),
        ("P99 延迟（ms）", f"{float(lat.get('p99', 0)):.2f}"),
        ("总请求数", f"{int(req['total'])}"),
        ("建连失败次数", f"{int(conn['connect_failures'])}"),
        ("关闭连接失败次数", f"{int(conn['close_failures'])}"),
    ]


def _summary_metrics_html(report: Dict) -> str:
    items = _summary_metric_items(report)
    rows = "\n".join(f"  <li>{label}：{value}</li>" for label, value in items)
    return f"<ul>\n{rows}\n</ul>"


def _write_summary_html(report: Dict, output_dir: Path) -> None:
    html = f"""<!doctype html>
<html lang="zh-CN">
<head><meta charset="utf-8"><title>Stress Summary - {report["scenario"]}</title></head>
<body>
  <h2>压力测试关键指标 - {report["scenario"]}</h2>
  <p>运行时长: {report["runtime_seconds"]:.2f}s</p>
  {_summary_metrics_html(report)}
</body>
</html>
"""
    (output_dir / "charts.html").write_text(html, encoding="utf-8")


def _write_ramp_artifacts(report: Dict, output_dir: Path) -> None:
    steps = report.get("ramp_steps", [])
    if not steps:
        return

    charts_dir = output_dir / "charts"
    charts_dir.mkdir(exist_ok=True)

    csv_lines = [
        "step_index,phase,concurrency,hold_seconds,elapsed_start_s,elapsed_end_s,elapsed_mid_s,"
        "throughput_rps,p50_ms,p95_ms,p99_ms,success_rate,connect_failures,unexpected_error_rate"
    ]
    ts_lines = [
        "elapsed_s,phase,concurrency,throughput_rps,p95_ms,p99_ms,success_rate_pct,connect_failures"
    ]
    for i, step in enumerate(steps):
        req = step.get("requests", {})
        lat = step.get("latency_ms", {})
        conn = step.get("connection", {})
        split = _split_error_codes_by_expectation(step.get("error_codes", {}))
        total = int(req.get("total", 0))
        unexpected = sum(int(v) for v in split["unexpected"].values())
        uerr = (unexpected / total) if total else 0.0
        t_mid = float(step.get("elapsed_mid_s", 0))
        t_end = float(step.get("elapsed_end_s", t_mid))
        csv_lines.append(
            f"{i},{step.get('phase','')},{step.get('concurrency',0)},{step.get('hold_seconds',0)},"
            f"{step.get('elapsed_start_s',0):.2f},{step.get('elapsed_end_s',0):.2f},{t_mid:.2f},"
            f"{req.get('throughput_rps',0):.4f},{lat.get('p50',0):.2f},{lat.get('p95',0):.2f},"
            f"{lat.get('p99',0):.2f},{req.get('success_rate',0):.4f},"
            f"{conn.get('connect_failures',0)},{uerr:.6f}"
        )
        ts_lines.append(
            f"{t_end:.2f},{step.get('phase','')},{step.get('concurrency',0)},"
            f"{req.get('throughput_rps',0):.4f},{lat.get('p95',0):.2f},{lat.get('p99',0):.2f},"
            f"{float(req.get('success_rate',0)) * 100:.4f},{conn.get('connect_failures',0)}"
        )
    (output_dir / "steps.csv").write_text("\n".join(csv_lines) + "\n", encoding="utf-8")
    (output_dir / "timeseries.csv").write_text("\n".join(ts_lines) + "\n", encoding="utf-8")

    ka = report.get("knee_analysis", {})
    knee_times: set = set()
    knee_conc = set()
    if ka.get("first_knee_concurrency") is not None:
        knee_conc.add(int(ka["first_knee_concurrency"]))
    for knee in ka.get("knees", []):
        knee_conc.add(int(knee.get("concurrency", 0)))
    for step in steps:
        if int(step.get("concurrency", 0)) in knee_conc:
            knee_times.add(float(step.get("elapsed_end_s", 0)))

    times = [float(s.get("elapsed_end_s", s.get("elapsed_mid_s", 0))) for s in steps]
    rps_vals = [float(s.get("requests", {}).get("throughput_rps", 0)) for s in steps]
    p95_vals = [float(s.get("latency_ms", {}).get("p95", 0)) for s in steps]
    p99_vals = [float(s.get("latency_ms", {}).get("p99", 0)) for s in steps]
    success_vals = [float(s.get("requests", {}).get("success_rate", 0)) * 100.0 for s in steps]

    _write_combined_time_series_svg(
        charts_dir / "timeseries_combined.svg",
        times,
        [
            ("吞吐 (RPS)", rps_vals, "#4f46e5"),
            ("P95 延迟 (ms)", p95_vals, "#ea580c"),
            ("P99 延迟 (ms)", p99_vals, "#dc2626"),
            ("成功率 (%)", success_vals, "#059669"),
        ],
        knee_times,
    )

    report_html = output_dir / "report.html"
    knee_section = ""
    if ka:
        bn_zh = ka.get("bottleneck_type_zh", bottleneck_label(str(ka.get("bottleneck_type", ""))))
        knee_section = f"""
<h3>压力点分析</h3>
<ul>
  <li>推荐安全并发: {ka.get('recommended_safe_concurrency')}</li>
  <li>首个压力点: {ka.get('first_knee_concurrency')}</li>
  <li>瓶颈类型: {bn_zh}</li>
  <li>降压恢复: {'是' if ka.get('recovery_ok') else '否'}</li>
</ul>"""
    netem = report.get("runner", {}).get("netem_rule", "(none)")
    report_html.write_text(
        f"""<!doctype html>
<html lang="zh-CN">
<head><meta charset="utf-8"><title>Stress Report - {report['scenario']}</title></head>
<body>
  <h1>压测报告: {report['scenario']}</h1>
  <p>运行时长: {report['runtime_seconds']:.2f}s | 模式: {report.get('runner', {}).get('mode', 'single')} | 网络: {netem}</p>
  {knee_section}
  <h3>时间序列折线图（横轴: 运行时间 s）</h3>
  <p>纵轴为各指标相对本 run 峰值的归一化百分比 (0–100%)，便于同图对比趋势；图例标注原始指标名称。</p>
  <img src="charts/timeseries_combined.svg" alt="combined timeseries" />
  <h3>关键指标汇总</h3>
  {_summary_metrics_html(report)}
  <h3>阶梯数据</h3>
  <pre>{chr(10).join(csv_lines)}</pre>
</body>
</html>""",
        encoding="utf-8",
    )


def _normalize_series(values: List[float]) -> List[float]:
    peak = max(values) if values else 0.0
    if peak <= 0:
        return [0.0 for _ in values]
    return [v / peak * 100.0 for v in values]


def _write_combined_time_series_svg(
    path: Path,
    times: List[float],
    series: List[tuple],
    knee_times: set,
) -> None:
    """Draw multiple normalized (0-100%) lines on one chart with legend."""
    if not times or not series:
        return
    width, height = 860, 480
    pad_l, pad_b, pad_t, pad_r = 80, 55, 70, 40
    plot_w = width - pad_l - pad_r
    plot_h = height - pad_t - pad_b
    max_t = max(times) if times else 1.0
    if max_t <= 0:
        max_t = 1.0

    def px(t: float) -> float:
        return pad_l + (t / max_t) * plot_w

    def py_pct(pct: float) -> float:
        return pad_t + plot_h - (pct / 100.0) * plot_h

    lines_svg = ""
    legend_svg = ""
    for i, (label, values, color) in enumerate(series):
        norm = _normalize_series(values)
        points = " ".join(f"{px(t):.1f},{py_pct(v):.1f}" for t, v in zip(times, norm))
        lines_svg += f'<polyline fill="none" stroke="{color}" stroke-width="2" points="{points}"/>'
        for t, v in zip(times, norm):
            lines_svg += f'<circle cx="{px(t):.1f}" cy="{py_pct(v):.1f}" r="3" fill="{color}"/>'
        lx = pad_l + i * 200
        ly = 28
        legend_svg += (
            f'<line x1="{lx}" y1="{ly}" x2="{lx + 24}" y2="{ly}" stroke="{color}" stroke-width="3"/>'
            f'<text x="{lx + 30}" y="{ly + 4}" font-size="12" fill="#333">{label}</text>'
        )

    knee_lines = ""
    for kt in sorted(knee_times):
        if kt <= 0:
            continue
        x = px(kt)
        knee_lines += (
            f'<line x1="{x:.1f}" y1="{pad_t}" x2="{x:.1f}" y2="{pad_t + plot_h}" '
            f'stroke="#f59e0b" stroke-width="1.5" stroke-dasharray="4,3"/>'
        )

    x_ticks = ""
    for frac in (0.0, 0.25, 0.5, 0.75, 1.0):
        tv = max_t * frac
        x = px(tv)
        x_ticks += (
            f'<line x1="{x:.1f}" y1="{pad_t + plot_h}" x2="{x:.1f}" y2="{pad_t + plot_h + 4}" stroke="#999"/>'
            f'<text x="{x:.1f}" y="{pad_t + plot_h + 18}" font-size="11" fill="#666" text-anchor="middle">{tv:.0f}s</text>'
        )

    y_ticks = ""
    for pct in (0, 25, 50, 75, 100):
        y = py_pct(float(pct))
        y_ticks += (
            f'<line x1="{pad_l - 4}" y1="{y:.1f}" x2="{pad_l}" y2="{y:.1f}" stroke="#999"/>'
            f'<text x="{pad_l - 8}" y="{y + 4:.1f}" font-size="11" fill="#666" text-anchor="end">{pct}%</text>'
        )

    svg = f"""<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">
  <rect width="100%" height="100%" fill="#fff"/>
  <text x="{pad_l}" y="52" font-size="15" fill="#111">压力指标 vs 运行时间（归一化）</text>
  {legend_svg}
  <line x1="{pad_l}" y1="{pad_t + plot_h}" x2="{pad_l + plot_w}" y2="{pad_t + plot_h}" stroke="#ccc"/>
  <line x1="{pad_l}" y1="{pad_t}" x2="{pad_l}" y2="{pad_t + plot_h}" stroke="#ccc"/>
  {x_ticks}{y_ticks}{knee_lines}
  {lines_svg}
  <text x="{pad_l + plot_w / 2:.0f}" y="{height - 12}" font-size="12" fill="#444" text-anchor="middle">运行时间 (s)</text>
  <text x="16" y="{pad_t + plot_h / 2:.0f}" font-size="12" fill="#444" transform="rotate(-90 16 {pad_t + plot_h / 2:.0f})" text-anchor="middle">归一化 (%)</text>
  <line x1="{pad_l + plot_w - 120}" y1="{pad_t + 12}" x2="{pad_l + plot_w - 100}" y2="{pad_t + 12}" stroke="#f59e0b" stroke-width="1.5" stroke-dasharray="4,3"/>
  <text x="{pad_l + plot_w - 95}" y="{pad_t + 16}" font-size="10" fill="#666">压力点</text>
</svg>"""
    path.write_text(svg, encoding="utf-8")


def _write_time_series_svg(
    path: Path,
    title: str,
    times: List[float],
    values: List[float],
    knee_times: set,
    y_unit: str,
    color: str,
    y_max: Optional[float] = None,
) -> None:
    if not times or not values:
        return
    width, height = 720, 360
    pad_l, pad_b, pad_t, pad_r = 70, 50, 40, 30
    plot_w = width - pad_l - pad_r
    plot_h = height - pad_t - pad_b
    max_t = max(times) if times else 1.0
    if max_t <= 0:
        max_t = 1.0
    max_y = y_max if y_max is not None else max(values)
    if max_y <= 0:
        max_y = 1.0

    def px(t: float) -> float:
        return pad_l + (t / max_t) * plot_w

    def py(v: float) -> float:
        return pad_t + plot_h - (v / max_y) * plot_h

    points = " ".join(f"{px(t):.1f},{py(v):.1f}" for t, v in zip(times, values))
    circles = []
    for t, v in zip(times, values):
        fill = "#f59e0b" if t in knee_times else color
        r = 7 if t in knee_times else 4
        circles.append(f'<circle cx="{px(t):.1f}" cy="{py(v):.1f}" r="{r}" fill="{fill}"/>')

    knee_lines = ""
    for kt in sorted(knee_times):
        if kt <= 0:
            continue
        x = px(kt)
        knee_lines += (
            f'<line x1="{x:.1f}" y1="{pad_t}" x2="{x:.1f}" y2="{pad_t + plot_h}" '
            f'stroke="#f59e0b" stroke-width="1.5" stroke-dasharray="4,3"/>'
        )

    # X-axis ticks (0, 25%, 50%, 75%, 100% of runtime)
    x_ticks = ""
    for frac in (0.0, 0.25, 0.5, 0.75, 1.0):
        tv = max_t * frac
        x = px(tv)
        x_ticks += (
            f'<line x1="{x:.1f}" y1="{pad_t + plot_h}" x2="{x:.1f}" y2="{pad_t + plot_h + 4}" stroke="#999"/>'
            f'<text x="{x:.1f}" y="{pad_t + plot_h + 18}" font-size="11" fill="#666" text-anchor="middle">{tv:.0f}s</text>'
        )

    y_ticks = ""
    for frac in (0.0, 0.5, 1.0):
        yv = max_y * frac
        y = py(yv)
        y_ticks += (
            f'<line x1="{pad_l - 4}" y1="{y:.1f}" x2="{pad_l}" y2="{y:.1f}" stroke="#999"/>'
            f'<text x="{pad_l - 8}" y="{y + 4:.1f}" font-size="11" fill="#666" text-anchor="end">{yv:.1f}</text>'
        )

    svg = f"""<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">
  <rect width="100%" height="100%" fill="#fff"/>
  <text x="{pad_l}" y="24" font-size="15" fill="#111">{title}</text>
  <text x="{width - pad_r}" y="24" font-size="11" fill="#666" text-anchor="end">纵轴: {y_unit}</text>
  <line x1="{pad_l}" y1="{pad_t + plot_h}" x2="{pad_l + plot_w}" y2="{pad_t + plot_h}" stroke="#ccc"/>
  <line x1="{pad_l}" y1="{pad_t}" x2="{pad_l}" y2="{pad_t + plot_h}" stroke="#ccc"/>
  {x_ticks}{y_ticks}{knee_lines}
  <polyline fill="none" stroke="{color}" stroke-width="2" points="{points}"/>
  {''.join(circles)}
  <text x="{pad_l + plot_w / 2:.0f}" y="{height - 8}" font-size="12" fill="#444" text-anchor="middle">运行时间 (s)</text>
</svg>"""
    path.write_text(svg, encoding="utf-8")
