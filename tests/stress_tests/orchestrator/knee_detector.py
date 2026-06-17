"""Detect stress knee points from ramp step reports."""

from __future__ import annotations

from typing import Any, Dict, List, Optional

BOTTLENECK_LABELS: Dict[str, str] = {
    "throughput_plateau": "吞吐平台期",
    "latency_spike": "延迟激增",
    "unexpected_errors": "非预期错误",
    "connect_failures": "建连失败",
    "none": "无明显瓶颈",
    "unknown": "未知",
}


def bottleneck_label(key: str) -> str:
    return BOTTLENECK_LABELS.get(key, key)


def reasons_labels(reasons: List[str]) -> List[str]:
    return [bottleneck_label(r) for r in reasons]


def _unexpected_error_rate(step: Dict[str, Any]) -> float:
    req = step.get("requests", {})
    total = int(req.get("total", 0))
    if total == 0:
        return 0.0
    err = step.get("error_codes", {})
    unexpected = 0
    for code_str, count in err.items():
        code = int(code_str)
        if (code & 4) != 0:  # ERROR bit
            unexpected += int(count)
        elif (code & 2) == 0 and code != 1 and code != -1:
            unexpected += int(count)
    # Also check threshold_result if present
    tr = step.get("threshold_detail", {})
    if "unexpected_error_rate" in tr:
        return float(tr["unexpected_error_rate"])
    return unexpected / float(total)


def detect_knees(
    steps: List[Dict[str, Any]],
    thresholds: Dict[str, Any],
) -> Dict[str, Any]:
    max_unexpected = float(thresholds.get("max_unexpected_error_rate", 0.001))
    max_connect = int(thresholds.get("max_connect_failures", 0))
    ramp_steps = [s for s in steps if s.get("phase") in ("warmup", "ramp_up")]
    recovery_steps = [s for s in steps if s.get("phase") == "recovery"]

    knees: List[Dict[str, Any]] = []
    safe_concurrency: Optional[int] = None
    first_knee_concurrency: Optional[int] = None

    baseline_p99 = None
    if ramp_steps:
        baseline_p99 = float(ramp_steps[0].get("latency_ms", {}).get("p99", 0))

    prev_rps = None
    prev_p99 = None

    for step in ramp_steps:
        conc = int(step.get("concurrency", 0))
        rps = float(step.get("requests", {}).get("throughput_rps", 0))
        p99 = float(step.get("latency_ms", {}).get("p99", 0))
        err_rate = _unexpected_error_rate(step)
        connect_fail = int(step.get("connection", {}).get("connect_failures", 0))

        reasons: List[str] = []
        is_knee = False

        if prev_rps is not None and prev_rps > 0:
            growth = (rps - prev_rps) / prev_rps
            if growth < 0.10 and step.get("phase") == "ramp_up":
                reasons.append("throughput_plateau")
                is_knee = True

        if prev_p99 is not None and prev_p99 > 0 and p99 / prev_p99 > 1.5:
            reasons.append("latency_spike")
            is_knee = True

        if err_rate > max_unexpected:
            reasons.append("unexpected_errors")
            is_knee = True

        if connect_fail > max_connect:
            reasons.append("connect_failures")
            is_knee = True

        if is_knee:
            knees.append(
                {
                    "concurrency": conc,
                    "phase": step.get("phase"),
                    "reasons": reasons,
                    "reasons_zh": reasons_labels(reasons),
                    "throughput_rps": rps,
                    "p99_ms": p99,
                    "unexpected_error_rate": err_rate,
                }
            )
            if first_knee_concurrency is None:
                first_knee_concurrency = conc
        else:
            safe_concurrency = conc

        prev_rps = rps
        prev_p99 = p99

    recovery_ok = True
    if recovery_steps and baseline_p99 and baseline_p99 > 0:
        last = recovery_steps[-1]
        final_p99 = float(last.get("latency_ms", {}).get("p99", 0))
        final_err = _unexpected_error_rate(last)
        if final_p99 > baseline_p99 * 1.2 or final_err > max_unexpected:
            recovery_ok = False

    bottleneck = "none"
    if knees:
        bottleneck = knees[0]["reasons"][0] if knees[0]["reasons"] else "unknown"

    return {
        "knees": knees,
        "recommended_safe_concurrency": safe_concurrency,
        "first_knee_concurrency": first_knee_concurrency,
        "recovery_ok": recovery_ok,
        "bottleneck_type": bottleneck,
        "bottleneck_type_zh": bottleneck_label(bottleneck),
    }
