#!/usr/bin/env python3
import argparse
import asyncio
import json
import os
import shutil
import sys
from concurrent.futures import ThreadPoolExecutor
from datetime import datetime
from pathlib import Path

from metrics import RunMetrics, evaluate_thresholds, write_outputs
from orchestrator.knee_detector import detect_knees
from orchestrator.netem import apply_netem, clear_netem
from orchestrator.ramp import build_ramp_sequence, load_ramp_profile
from scenarios import SCENARIO_REGISTRY

STRESS_ROOT = Path(__file__).resolve().parent


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="TCP stress test runner with ramp/netem support")
    parser.add_argument("--config", required=True, help="Path to scenario json config")
    parser.add_argument("--threads", type=int, default=0, help="Worker thread count override")
    parser.add_argument("--output-dir", default="", help="Optional output base directory")
    parser.add_argument(
        "--duration-seconds",
        type=int,
        default=0,
        help="Override scenario duration_seconds (non-ramp mode)",
    )
    parser.add_argument(
        "--concurrency",
        type=int,
        default=0,
        help="Override scenario concurrency (non-ramp mode)",
    )
    parser.add_argument("--ramp", action="store_true", help="Enable step ramp-up and recovery")
    parser.add_argument(
        "--ramp-config",
        default="",
        help="Ramp profile yaml/json path (default: configs/ramp_default.yaml)",
    )
    parser.add_argument(
        "--max-concurrency",
        type=int,
        default=0,
        help="Cap ramp step concurrency",
    )
    parser.add_argument(
        "--netem",
        default="",
        help='tc netem rule, e.g. "delay 50ms 10ms loss 0.5%". Empty = no interference',
    )
    parser.add_argument("--netem-dev", default="", help="Network device for netem")
    parser.add_argument(
        "--skip-netem",
        action="store_true",
        help="Do not apply netem even if --netem is set (for dry-run)",
    )
    return parser.parse_args()


def _build_output_paths(
    scenario_name: str,
    output_dir_override: str,
    *,
    mode: str,
    netem_rule: str,
) -> tuple[Path, Path]:
    base_dir = (
        Path(output_dir_override)
        if output_dir_override
        else STRESS_ROOT / "output" / scenario_name
    )
    mode_tag = "ramp" if mode == "ramp" else "single"
    net_tag = "net-weak" if netem_rule.strip() else "net-normal"
    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    run_dir = base_dir / f"{timestamp}_{mode_tag}_{net_tag}"
    return base_dir, run_dir


def _sync_latest(run_dir: Path, latest_dir: Path) -> None:
    latest_dir.mkdir(parents=True, exist_ok=True)
    for name in ("result.json", "summary.txt", "charts.html", "steps.csv", "timeseries.csv", "report.html"):
        src = run_dir / name
        if src.exists():
            shutil.copy2(src, latest_dir / name)
    charts_src = run_dir / "charts"
    if charts_src.is_dir():
        charts_dst = latest_dir / "charts"
        if charts_dst.exists():
            shutil.rmtree(charts_dst)
        shutil.copytree(charts_src, charts_dst)


def _split_concurrency(total: int, parts: int) -> list[int]:
    parts = max(parts, 1)
    total = max(total, 1)
    base = total // parts
    rem = total % parts
    chunks = [base + (1 if i < rem else 0) for i in range(parts)]
    return [c for c in chunks if c > 0]


def _run_thread_scenario(scenario_name: str, config: dict) -> RunMetrics:
    metrics = RunMetrics()
    asyncio.run(SCENARIO_REGISTRY[scenario_name](config, metrics))
    metrics.finalize()
    return metrics


def _execute_scenario(
    scenario_name: str,
    config: dict,
    threads_override: int,
) -> tuple[RunMetrics, dict]:
    total_concurrency = int(config.get("concurrency", 1))
    config_threads = int(config.get("threads", 1))
    thread_count = threads_override if threads_override > 0 else config_threads
    thread_count = max(1, min(thread_count, total_concurrency, (os.cpu_count() or 1) * 4))
    chunks = _split_concurrency(total_concurrency, thread_count)

    with ThreadPoolExecutor(max_workers=len(chunks)) as executor:
        futures = []
        for chunk in chunks:
            cfg = dict(config)
            cfg["concurrency"] = chunk
            futures.append(executor.submit(_run_thread_scenario, scenario_name, cfg))
        partials = [f.result() for f in futures]

    merged = RunMetrics()
    if partials:
        merged.started_at = min(p.started_at for p in partials)
        merged.ended_at = max(p.ended_at for p in partials)
        for p in partials:
            merged.merge_from(p)
    else:
        merged.finalize()

    runner = {
        "thread_count": len(chunks),
        "total_concurrency": total_concurrency,
        "per_thread_concurrency": chunks,
    }
    return merged, runner


def _step_report(step_meta: dict, metrics: RunMetrics, scenario_name: str, config: dict) -> dict:
    report = metrics.to_report(scenario_name=scenario_name)
    report.update(step_meta)
    tr = evaluate_thresholds(report, config.get("thresholds", {}))
    report["threshold_passed"] = tr["passed"]
    split_total = int(report["requests"].get("total", 0))
    unexpected = 0
    for code_str, count in report.get("error_codes", {}).items():
        if (int(code_str) & 4) != 0:
            unexpected += int(count)
    report["threshold_detail"] = {
        "unexpected_error_rate": (unexpected / split_total) if split_total else 0.0,
    }
    return report


async def main_async(args: argparse.Namespace) -> None:
    config_path = Path(args.config)
    if not config_path.is_absolute():
        config_path = Path.cwd() / config_path
    config = json.loads(config_path.read_text(encoding="utf-8"))
    scenario_name = str(config["name"])
    if scenario_name not in SCENARIO_REGISTRY:
        raise ValueError(f"Unsupported scenario: {scenario_name}")

    if args.duration_seconds > 0:
        config["duration_seconds"] = args.duration_seconds
    if args.concurrency > 0:
        config["concurrency"] = args.concurrency

    base_dir, run_dir = _build_output_paths(
        scenario_name,
        args.output_dir,
        mode="ramp" if args.ramp else "single",
        netem_rule=args.netem.strip(),
    )
    netem_rule = args.netem.strip()
    netem_dev = args.netem_dev.strip() or None
    output_tag = f"{'ramp' if args.ramp else 'single'}_{'net-weak' if netem_rule else 'net-normal'}"

    if not args.skip_netem:
        apply_netem(netem_rule, netem_dev)
    try:
        if args.ramp:
            ramp_path = args.ramp_config or str(STRESS_ROOT / "configs" / "ramp_default.yaml")
            profile = load_ramp_profile(ramp_path)
            max_conc = args.max_concurrency if args.max_concurrency > 0 else None
            sequence = build_ramp_sequence(
                profile,
                base_concurrency=int(config.get("concurrency", 1)),
                max_concurrency=max_conc,
            )
            step_reports = []
            all_step_metrics: list[RunMetrics] = []
            elapsed = 0.0
            for step in sequence:
                step_cfg = dict(config)
                step_cfg["concurrency"] = int(step["concurrency"])
                step_cfg["duration_seconds"] = int(step["hold_seconds"])
                hold = float(step["hold_seconds"])
                elapsed_start = elapsed
                elapsed_end = elapsed + hold
                elapsed = elapsed_end
                print(
                    f"ramp step phase={step['phase']} concurrency={step['concurrency']} "
                    f"hold={step['hold_seconds']}s t={elapsed_start:.0f}-{elapsed_end:.0f}s"
                )
                metrics, runner = _execute_scenario(scenario_name, step_cfg, args.threads)
                all_step_metrics.append(metrics)
                step_report = _step_report(
                    {
                        "phase": step["phase"],
                        "concurrency": step["concurrency"],
                        "hold_seconds": step["hold_seconds"],
                        "elapsed_start_s": elapsed_start,
                        "elapsed_end_s": elapsed_end,
                        "elapsed_mid_s": (elapsed_start + elapsed_end) / 2.0,
                    },
                    metrics,
                    scenario_name,
                    config,
                )
                step_report["runner"] = {
                    "thread_count": runner["thread_count"],
                    "total_concurrency": runner["total_concurrency"],
                    "per_thread_concurrency": runner["per_thread_concurrency"],
                }
                step_reports.append(step_report)

            aggregate = RunMetrics()
            if all_step_metrics:
                aggregate.started_at = min(m.started_at for m in all_step_metrics)
                aggregate.ended_at = max(m.ended_at for m in all_step_metrics)
                for m in all_step_metrics:
                    aggregate.merge_from(m)
            else:
                aggregate.finalize()

            last_runner = step_reports[-1].get("runner", {}) if step_reports else {}
            report = aggregate.to_report(scenario_name=scenario_name)
            report["runner"] = {
                "mode": "ramp",
                "thread_count": last_runner.get("thread_count", int(config.get("threads", 1))),
                "total_concurrency": step_reports[-1]["concurrency"] if step_reports else 0,
                "per_thread_concurrency": last_runner.get("per_thread_concurrency", []),
                "netem_rule": netem_rule or "(none)",
                "output_tag": output_tag,
                "ramp_config": ramp_path,
            }
            report["ramp_steps"] = step_reports
            report["knee_analysis"] = detect_knees(step_reports, config.get("thresholds", {}))
            report["threshold_result"] = evaluate_thresholds(report, config.get("thresholds", {}))
            ramp_failures = [s for s in step_reports if not s.get("threshold_passed", True)]
            if ramp_failures:
                report["threshold_result"]["passed"] = False
                report["threshold_result"]["checks"].append(
                    {
                        "name": "ramp_steps_all_pass",
                        "passed": False,
                        "actual": len(ramp_failures),
                        "expected": 0,
                    }
                )
        else:
            metrics, runner = _execute_scenario(scenario_name, config, args.threads)
            report = metrics.to_report(scenario_name=scenario_name)
            report["runner"] = {
                "mode": "single",
                "thread_count": runner["thread_count"],
                "total_concurrency": runner["total_concurrency"],
                "per_thread_concurrency": runner["per_thread_concurrency"],
                "netem_rule": netem_rule or "(none)",
                "output_tag": output_tag,
            }
            report["threshold_result"] = evaluate_thresholds(report, config.get("thresholds", {}))

        write_outputs(report, run_dir)
        _sync_latest(run_dir, base_dir / "latest")

        req = report["requests"]
        lat = report["latency_ms"]
        print(f"scenario={scenario_name}")
        print(f"output={run_dir}")
        print(f"mode={report['runner'].get('mode', 'single')}")
        print(f"total={req['total']} success_rate={req['success_rate']:.4f}")
        print(f"p95={lat['p95']:.2f}ms rps={req['throughput_rps']:.2f}")
        if report.get("knee_analysis"):
            ka = report["knee_analysis"]
            print(f"safe_concurrency={ka.get('recommended_safe_concurrency')} knee={ka.get('first_knee_concurrency')}")
        print(f"threshold_passed={report['threshold_result']['passed']}")
        if not report["threshold_result"]["passed"]:
            failed_checks = [
                c for c in report["threshold_result"].get("checks", []) if not c.get("passed", False)
            ]
            if failed_checks:
                print("failed_threshold_checks:")
                for check in failed_checks:
                    print(
                        f"  - {check.get('name', 'unknown')}: "
                        f"actual={check.get('actual')} expected={check.get('expected')}"
                    )
            sys.exit(2)
    finally:
        if not args.skip_netem:
            clear_netem(netem_dev)


def main() -> None:
    args = parse_args()
    asyncio.run(main_async(args))


if __name__ == "__main__":
    main()
