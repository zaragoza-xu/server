import json
import math
import time
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List


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
        f"thread_count(线程数): {runner.get('thread_count', 1)}",
        f"total_concurrency(总并发): {runner.get('total_concurrency', req['total'])}",
        f"per_thread_concurrency(每线程并发): {runner.get('per_thread_concurrency', [])}",
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
    _write_chart_files(report, output_dir)


def _write_chart_files(report: Dict, output_dir: Path) -> None:
    metrics = {
        "throughput_rps": float(report["requests"]["throughput_rps"]),
        "success_rate_pct": float(report["requests"]["success_rate"]) * 100.0,
        "latency_p95_ms": float(report["latency_ms"]["p95"]),
        "latency_p99_ms": float(report["latency_ms"]["p99"]),
        "connect_failures": float(report["connection"]["connect_failures"]),
    }

    # Normalize values for a compact horizontal bar chart.
    max_value = max(metrics.values()) if metrics else 1.0
    if max_value <= 0:
        max_value = 1.0

    width = 880
    row_h = 56
    left = 250
    chart_w = 560
    height = 80 + row_h * len(metrics)

    rows = []
    labels = [
        ("throughput_rps", "吞吐(req/s)"),
        ("success_rate_pct", "成功率(%)"),
        ("latency_p95_ms", "P95延迟(ms)"),
        ("latency_p99_ms", "P99延迟(ms)"),
        ("connect_failures", "建连失败次数"),
    ]
    for i, (key, label) in enumerate(labels):
        y = 40 + i * row_h
        v = metrics[key]
        bar_w = 0 if max_value == 0 else (v / max_value) * chart_w
        rows.append(
            f'<text x="20" y="{y+20}" font-size="14" fill="#333">{label}</text>'
            f'<rect x="{left}" y="{y}" width="{chart_w}" height="24" fill="#eef2ff" rx="4"/>'
            f'<rect x="{left}" y="{y}" width="{bar_w:.2f}" height="24" fill="#4f46e5" rx="4"/>'
            f'<text x="{left + chart_w + 10}" y="{y+18}" font-size="14" fill="#111">{v:.2f}</text>'
        )

    svg = (
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}">'
        f'<rect width="100%" height="100%" fill="#ffffff"/>'
        f'<text x="20" y="24" font-size="18" fill="#111">压力测试关键指标图</text>'
        + "".join(rows)
        + "</svg>"
    )

    (output_dir / "charts.svg").write_text(svg, encoding="utf-8")

    html = f"""<!doctype html>
<html lang="zh-CN">
<head><meta charset="utf-8"><title>Stress Charts</title></head>
<body>
  <h2>压力测试关键指标图 - {report["scenario"]}</h2>
  <p>运行时长: {report["runtime_seconds"]:.2f}s</p>
  <img src="charts.svg" alt="stress charts" />
</body>
</html>
"""
    (output_dir / "charts.html").write_text(html, encoding="utf-8")
