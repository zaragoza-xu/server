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
from scenarios import SCENARIO_REGISTRY


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Short-connection TCP stress test runner")
    parser.add_argument("--config", required=True, help="Path to scenario json config")
    parser.add_argument(
        "--threads",
        type=int,
        default=0,
        help="Worker thread count. 0 means use config.threads or 1.",
    )
    parser.add_argument(
        "--output-dir",
        default="",
        help="Optional output directory. Default: tests/stress_tests/output/<scenario_name>",
    )
    return parser.parse_args()


def _build_output_paths(scenario_name: str, output_dir_override: str) -> tuple[Path, Path]:
    base_dir = (
        Path(output_dir_override)
        if output_dir_override
        else Path("tests/stress_tests/output") / scenario_name
    )
    timestamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    run_dir = base_dir / timestamp
    return base_dir, run_dir


def _sync_latest(run_dir: Path, latest_dir: Path) -> None:
    latest_dir.mkdir(parents=True, exist_ok=True)
    for name in ("result.json", "summary.txt", "charts.svg", "charts.html"):
        src = run_dir / name
        if src.exists():
            shutil.copy2(src, latest_dir / name)


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


async def main_async(config_path: Path, output_dir_override: str, threads_override: int) -> None:
    config = json.loads(config_path.read_text(encoding="utf-8"))
    scenario_name = str(config["name"])
    if scenario_name not in SCENARIO_REGISTRY:
        raise ValueError(f"Unsupported scenario: {scenario_name}")
    base_dir, run_dir = _build_output_paths(scenario_name, output_dir_override)

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

    report = merged.to_report(scenario_name=scenario_name)
    report["runner"] = {
        "mode": "single_host_multi_thread_multi_coroutine",
        "thread_count": len(chunks),
        "total_concurrency": total_concurrency,
        "per_thread_concurrency": chunks,
    }
    report["threshold_result"] = evaluate_thresholds(report, config.get("thresholds", {}))
    write_outputs(report, run_dir)
    _sync_latest(run_dir, base_dir / "latest")

    print(f"scenario={scenario_name}")
    print(f"output={run_dir}")
    print(f"total={report['requests']['total']} success_rate={report['requests']['success_rate']:.4f}")
    print(f"p95={report['latency_ms']['p95']:.2f}ms rps={report['requests']['throughput_rps']:.2f}")
    print(f"threads={len(chunks)} per_thread_concurrency={chunks}")
    print(f"threshold_passed={report['threshold_result']['passed']}")
    if not report["threshold_result"]["passed"]:
        sys.exit(2)


def main() -> None:
    args = parse_args()
    config_path = Path(args.config)
    asyncio.run(
        main_async(
            config_path=config_path,
            output_dir_override=args.output_dir,
            threads_override=args.threads,
        )
    )


if __name__ == "__main__":
    main()
